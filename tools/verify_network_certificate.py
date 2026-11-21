"""Check WinHTTP certificate rejection using an untrusted loopback TLS proxy.

Build the probe with run_network_smoke_test.cmd first. Requires cryptography.
The generated certificate/key stay under a new ignored build/tests directory.
No certificate is installed, and no system proxy setting is changed.
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import socket
import ssl
import subprocess
import sys
import threading
import uuid

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    project = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=pathlib.Path,
                        default=project / "build/tests/network-smoke-bin/network-smoke-test.exe")
    args = parser.parse_args()
    if not args.probe.is_file():
        parser.error("build the network smoke probe first")
    root = project / "build/tests" / ("network-certificate-" + uuid.uuid4().hex[:12])
    root.mkdir(parents=True, exist_ok=False)
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "www.bing.com")])
    now = dt.datetime.now(dt.timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(name).issuer_name(name).public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - dt.timedelta(minutes=5))
        .not_valid_after(now + dt.timedelta(days=1))
        .add_extension(x509.SubjectAlternativeName([x509.DNSName("www.bing.com")]), critical=False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .sign(key, hashes.SHA256())
    )
    certificate_path, key_path = root / "untrusted.pem", root / "private-key.pem"
    certificate_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                          serialization.PrivateFormat.PKCS8,
                                          serialization.NoEncryption()))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certificate_path, key_path)
    stopped, connected = threading.Event(), threading.Event()
    observations: list[str] = []
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(4)
        listener.settimeout(0.2)
        port = listener.getsockname()[1]

        def serve() -> None:
            while not stopped.is_set():
                try:
                    client, _ = listener.accept()
                except TimeoutError:
                    continue
                with client:
                    client.settimeout(3)
                    try:
                        headers = b""
                        while b"\r\n\r\n" not in headers and len(headers) < 16384:
                            block = client.recv(4096)
                            if not block:
                                raise RuntimeError("proxy disconnected before CONNECT completed")
                            headers += block
                        if not headers.startswith(b"CONNECT www.bing.com:443 "):
                            raise RuntimeError("unexpected loopback proxy target")
                        observations.append(headers.split(b"\r\n", 1)[0].decode("ascii"))
                        client.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
                        connected.set()
                        with context.wrap_socket(client, server_side=True) as tls:
                            observations.append("Presented untrusted certificate over " + tls.version())
                            # Schannel may finish TLS before WinHTTP validates
                            # the peer. Only HTTP application data would show
                            # that validation had actually been bypassed.
                            if tls.recv(1):
                                observations.append("ERROR: HTTP request sent despite untrusted certificate")
                            else:
                                observations.append("Client closed without sending an HTTP request")
                    except (OSError, RuntimeError) as error:
                        observations.append(str(error))

        worker = threading.Thread(target=serve, name="loopback-certificate-probe")
        worker.start()
        try:
            result = subprocess.run(
                [str(args.probe.resolve()), "--root", str(root / "request"),
                 "--proxy", f"127.0.0.1:{port}", "--expect-error", "certificate"],
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15,
            )
        finally:
            stopped.set()
            worker.join(timeout=5)
        transcript = "\n".join(observations)
        (root / "server.log").write_text(transcript + "\n", encoding="utf-8")
        (root / "client.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        print(result.stdout, end="")
        print(result.stderr, end="")
        print(transcript)
        if worker.is_alive() or result.returncode or not connected.is_set():
            raise RuntimeError("certificate rejection test failed or did not reach the loopback TLS server")
        if "ERROR: HTTP request sent despite untrusted certificate" in transcript:
            raise RuntimeError("client accepted the untrusted certificate")
    print("PASS: untrusted TLS 1.2 certificate rejected; trust stores and system proxy unchanged.")
    print("Retained certificate test root:", root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
