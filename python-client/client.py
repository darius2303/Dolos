import os
import sys
from pathlib import Path
import requests
from zeep import Client
from zeep.exceptions import Fault, TransportError
from zeep.transports import Transport

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import Header, Footer, Button, Input, Log, Static, TabbedContent, TabPane, SelectionList
from textual.widgets.selection_list import Selection
from textual import work
from textual.worker import get_current_worker

WSDL_PATH = os.path.join(os.path.dirname(__file__), "../generated/ns.wsdl")
DEFAULT_ENDPOINT = "http://localhost:9090"
DEMO_FOLDER_PATH = Path(__file__).parent.parent / "demo"


class DolosClientApp(App):
    TITLE = "DOLOS - Plagiarism Detection Client"
    CSS = """
    Screen {
        align: center middle;
    }
    .status-bar {
        background: $accent;
        color: white;
        padding: 0 1;
        margin: 1 0;
        height: 1;
        text-align: center;
    }
    SelectionList {
        border: solid $accent;
        padding: 1;
        max-height: 8;
        margin-bottom: 1;
    }
    Log {
        border: solid $primary;
        margin-top: 1;
        min-height: 10;
    }
    Button {
        margin: 1 0;
        width: 100%;
    }
    Input {
        margin-bottom: 1;
    }
    """

    BINDINGS = [
        ("q", "quit", "Exit Application"),
        ("clear", "clear_logs", "Clear Activity Log")
    ]

    def __init__(self, endpoint: str = DEFAULT_ENDPOINT):
        super().__init__()
        self.endpoint = endpoint
        self.session_token = None
        self.soap_client = None

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Container():
            self.auth_status = Static("Status: Unauthenticated (No token)", classes="status-bar")
            yield self.auth_status

            with TabbedContent():
                with TabPane("Authentication", id="auth_tab"):
                    yield Input(placeholder="Username", id="username_input")
                    yield Input(placeholder="Password", password=True, id="password_input")
                    with Horizontal():
                        yield Button("Login", variant="primary", id="btn_login")
                        yield Button("Register", variant="success", id="btn_register")

                with TabPane("Plagiarism Analysis", id="analysis_tab"):
                    yield Static("Select files from the 'demo/' directory (Press Space or Click to select):")
                    yield SelectionList(id="files_selection_list")
                    yield Button("Submit Selected Files for Analysis", variant="warning", id="btn_analyze")

            self.activity_log = Log()
            yield Static("Activity & Server Responses:")
            yield self.activity_log

        yield Footer()

    def on_mount(self) -> None:
        self.activity_log.write_line(f"[INIT] Initializing gSOAP client proxy map targeting {self.endpoint}")
        self.init_soap_client()
        self.populate_demo_files()

    def populate_demo_files(self) -> None:
        """Scans the demo folder and injects files into the SelectionList widget."""
        selection_list = self.query_one("#files_selection_list", SelectionList)

        if not DEMO_FOLDER_PATH.exists():
            self.log_message(f"[ERROR] Local target path '{DEMO_FOLDER_PATH}' could not be located.")
            return

        demo_files = [f for f in DEMO_FOLDER_PATH.iterdir() if f.is_file()]

        if not demo_files:
            self.log_message("[WARN] The 'demo/' folder is completely empty.")
            return

        selections = []
        for file_path in demo_files:
            selections.append(Selection(file_path.name, str(file_path)))

        selection_list.clear_options()  # <--- MAKE SURE THIS SAYS clear_options()
        selection_list.add_options(selections)
        self.log_message(f"[LOCAL] Scanned 'demo/' directory. Detected {len(demo_files)} selectable files.")

    @work(exclusive=True, thread=True)
    def init_soap_client(self) -> None:
        try:
            session = requests.Session()
            transport = Transport(session=session, timeout=30)
            if os.path.exists(WSDL_PATH):
                wsdl = f"file://{os.path.abspath(WSDL_PATH)}"
            else:
                wsdl = f"{self.endpoint}?wsdl"

            self.soap_client = Client(wsdl=wsdl, transport=transport)
            self.soap_client.service._binding_options["address"] = self.endpoint
            self.call_from_thread(self.log_message, "[INIT] SOAP Binding mapping configured successfully.")
        except Exception as e:
            self.call_from_thread(self.log_message, f"[ERROR] Failed connecting to SOAP endpoint: {e}")

    def log_message(self, message: str) -> None:
        self.activity_log.write_line(message)

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "btn_login":
            self.run_auth_action(action="login")
        elif event.button.id == "btn_register":
            self.run_auth_action(action="register")
        elif event.button.id == "btn_analyze":
            self.run_analysis()

    @work(exclusive=True, thread=True)
    def run_auth_action(self, action: str) -> None:
        username = self.query_one("#username_input").value
        password = self.query_one("#password_input").value

        if not username or not password:
            self.call_from_thread(self.log_message, "[WARN] Username and password cannot be empty.")
            return

        if not self.soap_client:
            self.call_from_thread(self.log_message, "[ERROR] SOAP Engine is not online.")
            return

        try:
            if action == "register":
                self.call_from_thread(self.log_message, f"[SOAP] Invoking ns__register for user: {username}")
                try:
                    res = self.soap_client.service.register(username=username, password=password)
                except AttributeError:
                    res = self.soap_client.service.ns__register(username=username, password=password)

                if res == 1 or res == 0:
                    self.call_from_thread(self.log_message, "[SERVER] Registration reported SUCCESS.")
                else:
                    self.call_from_thread(self.log_message, f"[SERVER] Registration rejected. Code: {res}")

            elif action == "login":
                self.call_from_thread(self.log_message, f"[SOAP] Invoking ns__login for user: {username}")
                try:
                    token = self.soap_client.service.login(username=username, password=password)
                except AttributeError:
                    token = self.soap_client.service.ns__login(username=username, password=password)

                if token and token != "LOGIN_FAILED" and token != "DB_ERROR":
                    self.session_token = token
                    self.call_from_thread(self.update_auth_ui, f"Authenticated Token: {token}")
                    self.call_from_thread(self.log_message, f"[SERVER] Login Authorized. Session Token assigned.")
                else:
                    self.call_from_thread(self.log_message, f"[SERVER] Authentication failed: {token}")

        except Exception as exc:
            self.call_from_thread(self.log_message, f"[SOAP FAULT] Communication breakdown: {exc}")

    @work(exclusive=True, thread=True)
    def run_analysis(self) -> None:
        selected_paths = self.query_one("#files_selection_list", SelectionList).selected

        if not selected_paths:
            self.call_from_thread(self.log_message,
                                  "[WARN] Select files using Space bar or Left Click before submitting.")
            return

        if len(selected_paths) < 2:
            self.call_from_thread(self.log_message,
                                  "[WARN] Plagiarism analysis strictly requires at least 2 files selected.")
            return

        file_items = []
        for p_str in selected_paths:
            path = Path(p_str)
            if not path.is_file():
                self.call_from_thread(self.log_message, f"[ERROR] File missing locally: '{p_str}'")
                return
            file_items.append({"filename": path.name, "data": path.read_bytes()})

        array_of_files = {"item": file_items}
        self.call_from_thread(self.log_message, f"[SOAP] Transmitting {len(file_items)} targets to check...")

        try:
            try:
                report = self.soap_client.service.analyzeFiles(files=array_of_files)
            except AttributeError:
                report = self.soap_client.service.ns__analyzeFiles(files=array_of_files)

            self.call_from_thread(self.log_message,
                                  f"\n--- PLAGIARISM SYSTEM REPORT ---\n{report or 'No similarity markers flagged.'}")
        except Exception as exc:
            self.call_from_thread(self.log_message, f"[SERVER ERROR] Processing aborted: {exc}")

    def update_auth_ui(self, status_text: str) -> None:
        self.auth_status.update(status_text)

    def action_clear_logs(self) -> None:
        self.activity_log.clear()


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Dolos TUI Client Launch Gateway.")
    parser.add_argument("--endpoint", "-e", default=DEFAULT_ENDPOINT, help="Target service URL.")
    args = parser.parse_args()

    app = DolosClientApp(endpoint=args.endpoint)
    app.run()