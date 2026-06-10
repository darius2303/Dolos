import argparse
import os
import sys
from pathlib import Path

import requests
from zeep import Client
from zeep.exceptions import Fault, TransportError
from zeep.transports import Transport

WSDL_PATH = os.path.join(os.path.dirname(__file__), "../generated/ns.wsdl")
DEFAULT_ENDPOINT = "http://localhost:8080"


def build_client(endpoint: str) -> Client:
    session = requests.Session()
    transport = Transport(session=session, timeout=30)

    if os.path.exists(WSDL_PATH):
        wsdl = f"file://{os.path.abspath(WSDL_PATH)}"
    else:
        wsdl = f"{endpoint}?wsdl"

    client = Client(wsdl=wsdl, transport=transport)
    client.service._binding_options["address"] = endpoint
    return client


def read_files(paths: list[str]) -> list[dict]:
    items = []
    for path_str in paths:
        path = Path(path_str)
        if not path.is_file():
            print(
                f"[CLIENT] Error: '{path_str}' is not a file or does not exist.",
                file=sys.stderr,
            )
            sys.exit(1)

        data = path.read_bytes()
        print(f"[CLIENT] Loaded '{path.name}' ({len(data)} bytes)")
        items.append({"filename": path.name, "data": data})
    return items


def analyze_files(endpoint: str, file_paths: list[str]) -> str:
    client = build_client(endpoint)
    file_items = read_files(file_paths)
    array_of_files = {"item": file_items}

    print(f"[CLIENT] Sending {len(file_items)} files to {endpoint} ...")

    try:
        report = client.service.analyzeFiles(files=array_of_files)
    except Fault as exc:
        print(f"[CLIENT] SOAP Fault: {exc}", file=sys.stderr)
        sys.exit(1)
    except TransportError as exc:
        print(f"[CLIENT] Transport error: {exc}", file=sys.stderr)
        sys.exit(1)
    except requests.exceptions.ConnectionError:
        print(f"[CLIENT] Cannot connect to server at {endpoint}", file=sys.stderr)
        sys.exit(1)

    return report or ""


def main():
    parser = argparse.ArgumentParser(
        description="Send files to the plagiarism-detection SOAP server."
    )
    parser.add_argument(
        "files",
        nargs="+",
        metavar="FILE",
        help="Files to compare (at least 2 required)",
    )
    parser.add_argument(
        "--endpoint",
        "-e",
        default=DEFAULT_ENDPOINT,
        metavar="URL",
        help=f"Server endpoint (default: {DEFAULT_ENDPOINT})",
    )

    args = parser.parse_args()

    if len(args.files) < 2:
        parser.error("At least 2 files are required for comparison.")

    report = analyze_files(args.endpoint, args.files)
    print(f"\n{report}")


if __name__ == "__main__":
    main()
