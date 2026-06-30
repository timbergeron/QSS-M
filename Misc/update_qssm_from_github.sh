#!/usr/bin/env sh
set -eu

PYTHON_BIN=${PYTHON_BIN:-python3}
PYTHON_PAYLOAD=$(cat <<'__QSSM_UPDATER_PYTHON_PAYLOAD_BELOW__'
#!/usr/bin/env python3
"""Download the latest successful QSS-M GitHub Actions artifact and update a folder."""

from __future__ import annotations

import argparse
import fnmatch
import getpass
import hashlib
import json
import os
import posixpath
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime
from pathlib import Path, PurePosixPath

if sys.argv:
    sys.argv[0] = "update_qssm_from_github.sh"

API_ROOT = "https://api.github.com"
APP_NAME = "QSSMUpdater"
CONFIG_FILE_NAME = "config.json"
CONFIG_VERSION = 2
DEFAULT_OWNER = "timbergeron"
DEFAULT_REPO = "QSS-M"
USER_AGENT = "qssm-latest-artifact-downloader"

PLATFORM_PRESETS = {
    "windows": {
        "label": "Windows 64-bit",
        "workflow_name": "Windows CI (MinGW Linux Cross)",
        "job_name": "w64",
        "artifact_name_hint": "QSS-M-w64.zip",
        "launch_glob": "*.exe",
    },
    "macos": {
        "label": "macOS universal",
        "workflow_name": "macOS CI (Xcode)",
        "job_name": "arm64-universal",
        "artifact_name_hint": "QSS-M-macOS.zip",
        "launch_glob": "*.app",
    },
    "linux": {
        "label": "Linux 64-bit",
        "workflow_name": "Linux CI",
        "job_name": "x64",
        "artifact_name_hint": "QSS-M-l64.zip",
        "launch_glob": "qss-m*,quakespasm*",
    },
}


class UpdaterError(RuntimeError):
    pass


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


ANSI_COLORS = {
    "bold": "1",
    "dim": "2",
    "red": "31",
    "green": "32",
    "yellow": "33",
    "blue": "34",
    "magenta": "35",
    "cyan": "36",
    "bold_cyan": "1;36",
}


def ui_color_enabled(stream=None) -> bool:
    stream = stream or sys.stdout
    return bool(
        getattr(stream, "isatty", lambda: False)()
        and not os.environ.get("NO_COLOR")
        and os.environ.get("TERM", "") != "dumb"
    )


def ui_emoji_enabled() -> bool:
    return os.environ.get("QSSM_NO_EMOJI", "").strip().lower() not in {"1", "true", "yes", "on"}


def ui_style(text: object, color: str | None = None, stream=None) -> str:
    value = str(text)
    code = ANSI_COLORS.get(color or "")
    if not code or not ui_color_enabled(stream):
        return value
    return f"\033[{code}m{value}\033[0m"


def ui_text(message: object, icon: str = "") -> str:
    if icon and ui_emoji_enabled():
        return f"{icon} {message}"
    return str(message)


def ui_line(message: object = "", icon: str = "", color: str | None = None) -> None:
    print(ui_style(ui_text(message, icon), color))


def ui_error(message: object = "", icon: str = "❌") -> None:
    print(ui_style(ui_text(message, icon), "red", sys.stderr), file=sys.stderr)


def ui_section(title: str, icon: str = "") -> None:
    print()
    print(ui_style(ui_text(title, icon), "bold_cyan"))
    print(ui_style("-" * 60, "dim"))


def ui_kv(label: str, value: object, icon: str = "•") -> None:
    marker = ui_text("", icon).strip()
    prefix = f"{marker} " if marker else ""
    print(f"{ui_style(prefix + label + ':', 'bold')} {value}")


def eprint(message: str = "") -> None:
    print(message, file=sys.stderr)


def first_non_empty(*values):
    for value in values:
        if value is not None and str(value).strip():
            return value
    return None


def default_platform() -> str:
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("win"):
        return "windows"
    return "linux"


def terminal_input_available() -> bool:
    if sys.stdin.isatty():
        return True
    if os.name != "posix":
        return False
    try:
        fd = os.open("/dev/tty", os.O_RDONLY)
    except OSError:
        return False
    try:
        return os.isatty(fd)
    finally:
        os.close(fd)


def can_prompt(args: argparse.Namespace) -> bool:
    return (not args.no_prompt) and terminal_input_available()


def prompt_input(prompt: str) -> str:
    if sys.stdin.isatty():
        return input(prompt)
    if os.name == "posix":
        try:
            with open("/dev/tty", "r+", encoding="utf-8", errors="replace") as tty:
                tty.write(prompt)
                tty.flush()
                value = tty.readline()
        except OSError as exc:
            raise EOFError from exc
        if not value:
            raise EOFError
        return value.rstrip("\r\n")
    raise EOFError


def prompt_default(label: str, default: str | None) -> str:
    suffix = f" [{default}]" if default else ""
    try:
        value = prompt_input(f"{ui_style(ui_text(label, '❯'), 'cyan')}{ui_style(suffix, 'dim')}: ").strip()
    except EOFError:
        return default or ""
    return default if not value else value


def prompt_yes_no(label: str, default: bool = True) -> bool:
    suffix = "Y/n" if default else "y/N"
    try:
        value = prompt_input(f"{ui_style(ui_text(label, '❯'), 'cyan')} {ui_style('[' + suffix + ']', 'dim')}: ").strip().lower()
    except EOFError:
        return default
    if not value:
        return default
    return value.startswith("y")


def prompt_secret(label: str) -> str:
    try:
        return getpass.getpass(f"{ui_style(ui_text(label, '🔒'), 'cyan')}: ").strip()
    except EOFError:
        return ""
    except Exception:
        try:
            return prompt_input(f"{ui_style(ui_text(label, '🔒'), 'cyan')}: ").strip()
        except EOFError:
            return ""


def clean_token(token: str | None) -> str | None:
    if not token or not str(token).strip():
        return None
    clean = str(token).strip().strip('"').strip("'").strip()
    if clean.lower().startswith("bearer "):
        clean = clean[7:].strip()
    return clean or None


def gh_cli_token() -> str | None:
    gh = shutil.which("gh")
    if not gh:
        return None
    try:
        proc = subprocess.run(
            [gh, "auth", "token"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return None
    if proc.returncode == 0 and proc.stdout:
        return clean_token(proc.stdout.splitlines()[0])
    return None


def resolve_token(args: argparse.Namespace, config: dict, include_prompt: bool) -> dict:
    token = clean_token(args.token)
    if token:
        return {"token": token, "source": "--token"}

    token = clean_token(os.environ.get("GITHUB_TOKEN"))
    if token:
        return {"token": token, "source": "GITHUB_TOKEN"}

    token = clean_token(config.get("token"))
    if token:
        return {"token": token, "source": "saved config"}

    token = gh_cli_token()
    if token:
        return {"token": token, "source": "gh auth token"}

    if include_prompt:
        token = clean_token(prompt_secret("GitHub token"))
        if token:
            return {"token": token, "source": "interactive prompt"}

    return {"token": None, "source": "none"}


def config_dir() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME")
    if base:
        return Path(base).expanduser() / APP_NAME
    return Path.home() / ".config" / APP_NAME


def default_config_path() -> Path:
    return config_dir() / CONFIG_FILE_NAME


def default_dest() -> Path:
    return Path.cwd().resolve()


def expand_path(raw: str | os.PathLike[str]) -> Path:
    return Path(os.path.expandvars(os.path.expanduser(str(raw)))).resolve()


def load_config(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        text = path.read_text(encoding="utf-8")
        return json.loads(text) if text.strip() else {}
    except Exception as exc:
        raise UpdaterError(f"Could not read config file: {path}\n{exc}") from exc


def save_config(path: Path, data: dict) -> None:
    copy = dict(data)
    copy["config_version"] = CONFIG_VERSION
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            path.parent.chmod(0o700)
        except OSError:
            pass
        tmp_path.write_text(json.dumps(copy, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        try:
            tmp_path.chmod(0o600)
        except OSError:
            pass
        tmp_path.replace(path)
    except Exception as exc:
        raise UpdaterError(f"Could not save config file: {path}\n{exc}") from exc


def normalize_platform(value: str | None) -> str:
    if not value or not str(value).strip():
        return default_platform()
    aliases = {
        "win": "windows",
        "windows": "windows",
        "w64": "windows",
        "mac": "macos",
        "macos": "macos",
        "darwin": "macos",
        "osx": "macos",
        "linux": "linux",
        "l64": "linux",
    }
    key = str(value).strip().lower()
    if key not in aliases:
        raise UpdaterError(f"Unknown platform '{value}'. Use one of: windows, macos, linux.")
    return aliases[key]


def parse_repo(raw: str) -> tuple[str, str]:
    repo = raw.strip().strip("/")
    if "/" not in repo:
        raise UpdaterError("Repository must use owner/repo format, for example timbergeron/QSS-M.")
    owner, name = repo.split("/", 1)
    owner = owner.strip()
    name = name.strip()
    if not owner or not name:
        raise UpdaterError("Repository must use owner/repo format, for example timbergeron/QSS-M.")
    return owner, name


def get_config_value(config: dict, key: str):
    return config.get(key) if isinstance(config, dict) else None


def interactive_setup(args: argparse.Namespace, config: dict, config_path: Path) -> dict:
    if not can_prompt(args):
        raise UpdaterError(
            "First-run setup needs an interactive terminal. Rerun without --no-prompt, "
            "or pass --dest and --token/--config from a terminal."
        )

    ui_section("QSS-M updater setup", "🛠️")
    ui_kv("Config", config_path, "📄")
    print()

    platform_default = normalize_platform(first_non_empty(args.platform, get_config_value(config, "platform")))
    platform_key = normalize_platform(prompt_default("Artifact platform (windows/macos/linux)", platform_default))
    preset = PLATFORM_PRESETS[platform_key]

    owner_default = first_non_empty(args.owner, get_config_value(config, "owner"), DEFAULT_OWNER)
    repo_default = first_non_empty(args.repo, get_config_value(config, "repo"), DEFAULT_REPO)
    owner, repo = parse_repo(prompt_default("GitHub repository", f"{owner_default}/{repo_default}"))

    dest_default = expand_path(first_non_empty(args.dest, get_config_value(config, "dest"), default_dest()))
    dest_dir = expand_path(prompt_default("QSS-M folder to update", str(dest_default)))

    new_config = dict(config)
    new_config["platform"] = platform_key
    new_config["owner"] = owner
    new_config["repo"] = repo
    new_config["dest"] = str(dest_dir)

    ui_section("Selected preset", "📦")
    ui_kv("Platform", preset["label"], "🖥️")
    ui_kv("Workflow", preset["workflow_name"], "⚙️")
    ui_kv("Job", preset["job_name"], "✅")
    ui_kv("Artifact", preset["artifact_name_hint"], "📦")
    print()

    existing = resolve_token(args, new_config, False)
    if existing["token"]:
        ui_line(f"GitHub token already available from: {existing['source']}", "🔑", "green")
        if prompt_yes_no("Keep using that token source", True):
            save_config(config_path, new_config)
            return new_config

    ui_line("GitHub Actions artifact downloads require a token.", "🔑", "yellow")
    ui_line("Fine-grained token permissions: Metadata Read-only and Actions Read-only.", "ℹ️", "dim")
    token = clean_token(prompt_secret("GitHub token, or Enter to use gh/GITHUB_TOKEN later"))
    if token:
        new_config["token"] = token
    else:
        new_config.pop("token", None)

    save_config(config_path, new_config)
    return new_config


def request_headers(token: str | None = None, accept: str = "application/vnd.github+json") -> dict:
    headers = {
        "Accept": accept,
        "User-Agent": USER_AGENT,
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def api_url(path: str, params: dict | None = None) -> str:
    base = path if path.startswith(("http://", "https://")) else f"{API_ROOT}{path}"
    if not params:
        return base
    query = urllib.parse.urlencode({k: v for k, v in params.items() if v is not None})
    return f"{base}?{query}"


def read_error_body(exc: urllib.error.HTTPError) -> str:
    try:
        body = exc.read(4096)
    except Exception:
        return ""
    try:
        return body.decode("utf-8", errors="replace")
    except Exception:
        return repr(body)


def github_json(path: str, token: str | None = None, params: dict | None = None):
    url = api_url(path, params)
    req = urllib.request.Request(url, headers=request_headers(token), method="GET")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as exc:
        body = read_error_body(exc)
        raise UpdaterError(f"GitHub API HTTP {exc.code} for {url}\n{body}") from exc
    except urllib.error.URLError as exc:
        raise UpdaterError(f"Network error calling GitHub API for {url}\n{exc}") from exc
    try:
        return json.loads(raw.decode("utf-8"))
    except Exception as exc:
        raise UpdaterError(f"GitHub API returned invalid JSON for {url}\n{exc}") from exc


def paged_list(path: str, key: str, token: str | None, params: dict | None = None, max_pages: int = 5) -> list:
    request_params = dict(params or {})
    request_params.setdefault("per_page", 100)
    items: list = []
    for page in range(1, max_pages + 1):
        request_params["page"] = page
        data = github_json(path, token, request_params)
        page_items = list(data.get(key) or [])
        items.extend(page_items)
        if len(page_items) < int(request_params["per_page"]):
            break
    return items


def default_branch(owner: str, repo: str, token: str | None) -> str:
    data = github_json(f"/repos/{owner}/{repo}", token)
    branch = data.get("default_branch")
    if not branch:
        raise UpdaterError("Could not determine the repo default branch.")
    return str(branch)


def branch_head_sha(owner: str, repo: str, branch: str, token: str | None) -> str:
    quoted = urllib.parse.quote(branch, safe="")
    data = github_json(f"/repos/{owner}/{repo}/branches/{quoted}", token)
    sha = (data.get("commit") or {}).get("sha")
    if not sha:
        raise UpdaterError(f"Could not determine latest commit SHA for branch '{branch}'.")
    return str(sha)


def find_matching_run(
    owner: str,
    repo: str,
    branch: str,
    head_sha: str,
    workflow_name: str,
    token: str | None,
    allow_older: bool,
) -> dict:
    runs = paged_list(
        f"/repos/{owner}/{repo}/actions/runs",
        "workflow_runs",
        token,
        {"branch": branch, "event": "push", "status": "completed"},
        5,
    )
    matching = [run for run in runs if str(run.get("name", "")).lower() == workflow_name.lower() and run.get("event") == "push"]
    if not matching:
        raise UpdaterError(f"No completed push runs found for workflow '{workflow_name}' on branch '{branch}'.")

    latest = [run for run in matching if run.get("head_sha") == head_sha]
    successful_latest = [run for run in latest if run.get("conclusion") == "success"]
    if successful_latest:
        return successful_latest[0]

    if not allow_older:
        statuses = ", ".join(
            f"run #{run.get('run_number')} conclusion={run.get('conclusion')}" for run in latest[:5]
        )
        if not statuses:
            statuses = "no completed run found for the latest commit yet"
        raise UpdaterError(
            "The latest commit does not have a successful completed run for this workflow yet.\n"
            f"Latest commit: {head_sha}\n"
            f"Found: {statuses}\n\n"
            "Run again after GitHub Actions finishes, or pass --allow-older to use the newest older successful artifact."
        )

    older_successes = [run for run in matching if run.get("conclusion") == "success"]
    if not older_successes:
        raise UpdaterError(f"No successful completed runs found for workflow '{workflow_name}'.")
    return older_successes[0]


def confirm_job_success(owner: str, repo: str, run_id: int, job_name: str, token: str | None) -> dict:
    jobs = paged_list(f"/repos/{owner}/{repo}/actions/runs/{run_id}/jobs", "jobs", token, {}, 3)
    exact = [job for job in jobs if str(job.get("name", "")).lower() == job_name.lower()]
    partial = [job for job in jobs if job_name.lower() in str(job.get("name", "")).lower()]
    candidates = exact or partial
    if not candidates:
        names = "\n".join(f"  - {job.get('name')}" for job in jobs)
        raise UpdaterError(f"Could not find job '{job_name}' in this workflow run.\nJobs found:\n{names}")

    successful = [job for job in candidates if job.get("conclusion") == "success"]
    if not successful:
        details = "\n".join(
            f"  - {job.get('name')}: status={job.get('status')} conclusion={job.get('conclusion')}"
            for job in candidates
        )
        raise UpdaterError(f"Matching job was not successful:\n{details}")
    return successful[0]


def artifact_score(artifact: dict, job_name: str, name_hint: str | None) -> int:
    name = str(artifact.get("name") or "")
    name_lc = name.lower()
    job_lc = job_name.lower()
    score = 0
    if artifact.get("expired"):
        score -= 10000
    if "log" in name_lc:
        score -= 500
    if name_hint:
        hint_lc = name_hint.lower()
        if name_lc == hint_lc:
            score += 1000
        elif hint_lc in name_lc:
            score += 500
    if name_lc == job_lc:
        score += 300
    elif job_lc in name_lc:
        score += 200
    if "qss-m" in name_lc or "qssm" in name_lc:
        score += 50
    if "linux" in name_lc or "l64" in name_lc:
        score += 25
    if "win" in name_lc or "mingw" in name_lc:
        score += 25
    if "mac" in name_lc or "macos" in name_lc:
        score += 25
    return score


def get_artifacts(
    owner: str,
    repo: str,
    run_id: int,
    token: str | None,
    job_name: str,
    name_hint: str | None,
    include_logs: bool,
) -> list[dict]:
    artifacts = paged_list(f"/repos/{owner}/{repo}/actions/runs/{run_id}/artifacts", "artifacts", token, {}, 3)
    artifacts = [artifact for artifact in artifacts if not artifact.get("expired")]
    if not include_logs:
        artifacts = [artifact for artifact in artifacts if "log" not in str(artifact.get("name", "")).lower()]

    if name_hint:
        hint_lc = name_hint.lower()
        hinted = [artifact for artifact in artifacts if hint_lc in str(artifact.get("name", "")).lower()]
        if hinted:
            artifacts = hinted
    else:
        job_lc = job_name.lower()
        job_named = [artifact for artifact in artifacts if job_lc in str(artifact.get("name", "")).lower()]
        if job_named:
            artifacts = job_named

    if not artifacts:
        if name_hint:
            hint_msg = f" matching artifact hint '{name_hint}'"
        else:
            hint_msg = f" matching job/artifact '{job_name}'"
        raise UpdaterError(f"No non-expired non-log artifacts found for this run{hint_msg}.")

    artifacts.sort(
        key=lambda artifact: (
            artifact_score(artifact, job_name, name_hint),
            int(artifact.get("size_in_bytes") or 0),
            str(artifact.get("name") or ""),
        ),
        reverse=True,
    )
    return artifacts[:1]


def require_token_for_download(token: str | None, config_path: Path) -> None:
    if token:
        return
    raise UpdaterError(
        "GitHub requires authentication to download Actions artifact ZIPs through the REST API.\n\n"
        "Do one of these, then rerun:\n\n"
        "  Option A, run setup again:\n"
        "    ./update_qssm_from_github.sh --setup\n\n"
        "  Option B, use GitHub CLI:\n"
        "    gh auth login\n\n"
        "  Option C, set a token in this terminal:\n"
        "    export GITHUB_TOKEN=github_pat_your_token_here\n"
        "    ./update_qssm_from_github.sh\n\n"
        f"Config path: {config_path}\n"
        "For a fine-grained token, use Metadata: Read-only and Actions: Read-only."
    )


def copy_response_to_file(resp, out_path: Path) -> dict:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    content_length_raw = resp.headers.get("Content-Length")
    content_length = int(content_length_raw) if content_length_raw and content_length_raw.isdigit() else -1
    bytes_written = 0
    with out_path.open("wb") as handle:
        while True:
            chunk = resp.read(1024 * 128)
            if not chunk:
                break
            handle.write(chunk)
            bytes_written += len(chunk)
    if content_length >= 0 and bytes_written != content_length:
        raise UpdaterError(f"Download ended early. Expected {content_length} bytes, wrote {bytes_written} bytes.")
    return {
        "bytes_written": bytes_written,
        "content_length": content_length,
        "content_type": resp.headers.get("Content-Type") or "",
        "status": getattr(resp, "status", 0),
        "url": resp.geturl(),
    }


def download_final_artifact_url(final_url: str, out_path: Path) -> dict:
    req = urllib.request.Request(final_url, headers=request_headers(None, "application/octet-stream"), method="GET")
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return copy_response_to_file(resp, out_path)
    except urllib.error.HTTPError as exc:
        body = read_error_body(exc)
        raise UpdaterError(f"Artifact storage download HTTP {exc.code}\n{body}") from exc
    except urllib.error.URLError as exc:
        raise UpdaterError(f"Network error downloading artifact storage: {exc}") from exc


def download_github_artifact_once(url: str, out_path: Path, token: str) -> dict:
    opener = urllib.request.build_opener(NoRedirectHandler)
    req = urllib.request.Request(url, headers=request_headers(token), method="GET")
    try:
        with opener.open(req, timeout=60) as resp:
            return copy_response_to_file(resp, out_path)
    except urllib.error.HTTPError as exc:
        if exc.code in {301, 302, 303, 307, 308}:
            location = exc.headers.get("Location")
            if not location:
                raise UpdaterError("GitHub artifact download redirected without a Location header.") from exc
            final_url = urllib.parse.urljoin(url, location)
            return download_final_artifact_url(final_url, out_path)
        body = read_error_body(exc)
        if exc.code in {401, 403}:
            raise UpdaterError(
                f"GitHub artifact download HTTP {exc.code}.\n\n"
                "Your token is missing, expired, malformed, or does not have Actions read access for this repo.\n"
                "For a fine-grained token, use Metadata: Read-only and Actions: Read-only.\n\n"
                f"Response:\n{body}"
            ) from exc
        raise UpdaterError(f"GitHub artifact download HTTP {exc.code}\n{body}") from exc
    except urllib.error.URLError as exc:
        raise UpdaterError(f"Network error downloading artifact: {exc}") from exc


def file_preview(path: Path, max_bytes: int = 600) -> str:
    if not path.is_file():
        return ""
    data = path.read_bytes()[:max_bytes]
    text = data.decode("utf-8", errors="replace")
    return "".join(ch if ch in "\t\r\n" or 32 <= ord(ch) <= 126 else "." for ch in text).strip()


def test_zip_readable(path: Path) -> str | None:
    if not path.is_file():
        return "Downloaded file was not created."
    if path.stat().st_size < 22:
        return f"Downloaded file is too small to be a ZIP archive ({path.stat().st_size} bytes)."
    try:
        with zipfile.ZipFile(path) as archive:
            archive.infolist()
    except Exception as exc:
        return str(exc)
    return None


def test_artifact_digest(path: Path, expected_digest: str | None) -> str | None:
    if not expected_digest:
        return None
    prefix = "sha256:"
    if not expected_digest.lower().startswith(prefix) or len(expected_digest) != len(prefix) + 64:
        return None
    expected = expected_digest[len(prefix) :].lower()
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected:
        return f"Downloaded artifact SHA-256 mismatch. Expected sha256:{expected} but got sha256:{actual}."
    return None


def invalid_download_message(
    artifact_name: str,
    zip_path: Path,
    problem: str,
    info: dict | None,
    expected_size: int,
) -> str:
    details = [f"Artifact: {artifact_name}"]
    if zip_path.exists():
        details.append(f"Downloaded bytes: {zip_path.stat().st_size:,}")
    if expected_size > 0:
        details.append(f"GitHub metadata size: {expected_size:,}")
    if info:
        if info.get("status"):
            details.append(f"HTTP status: {info['status']}")
        if info.get("content_type"):
            details.append(f"Content-Type: {info['content_type']}")
        if info.get("content_length", -1) >= 0:
            details.append(f"Content-Length: {info['content_length']:,}")
    preview = file_preview(zip_path)
    if preview:
        details.append(f"Response starts with:\n{preview}")
    return f"Downloaded artifact is not valid.\n{problem}\n\n" + "\n".join(details)


def download_github_artifact(
    url: str,
    out_path: Path,
    token: str | None,
    config_path: Path,
    artifact_name: str,
    expected_size: int = 0,
    expected_digest: str | None = None,
) -> None:
    require_token_for_download(token, config_path)
    if token is None:
        raise UpdaterError("GitHub token was not available after validation.")
    last_error: Exception | None = None
    for attempt in range(1, 4):
        if out_path.exists():
            out_path.unlink()
        info = None
        try:
            info = download_github_artifact_once(url, out_path, token)
            zip_problem = test_zip_readable(out_path)
            if zip_problem:
                raise UpdaterError(invalid_download_message(artifact_name, out_path, zip_problem, info, expected_size))
            digest_problem = test_artifact_digest(out_path, expected_digest)
            if digest_problem:
                raise UpdaterError(invalid_download_message(artifact_name, out_path, digest_problem, info, expected_size))
            return
        except Exception as exc:
            last_error = exc
            message = str(exc)
            retryable = (
                message.startswith("Downloaded artifact is not valid.")
                or message.startswith("Download ended early.")
                or message.startswith("Network error downloading artifact")
                or message.startswith("GitHub artifact download HTTP 5")
                or message.startswith("Artifact storage download HTTP 5")
            )
            if not retryable or attempt == 3:
                raise
            ui_line(f"Artifact download failed validation or transfer checks; retrying ({attempt + 1}/3)...", "⚠️", "yellow")
            time.sleep(2)
    if last_error:
        raise last_error


def safe_zip_target(root: Path, entry_name: str) -> Path:
    normalized = entry_name.replace("\\", "/")
    pure = PurePosixPath(normalized)
    if pure.is_absolute() or any(part in {"", ".", ".."} for part in pure.parts):
        raise UpdaterError(f"Blocked unsafe ZIP entry: {entry_name}")
    target = root.joinpath(*pure.parts)
    root_resolved = root.resolve()
    target_resolved = target.resolve(strict=False)
    try:
        target_resolved.relative_to(root_resolved)
    except ValueError as exc:
        raise UpdaterError(f"Blocked unsafe ZIP entry: {entry_name}") from exc
    return target


def safe_symlink_target(root: Path, link_path: Path, link_target: str, entry_name: str) -> str:
    normalized = link_target.replace("\\", "/")
    pure = PurePosixPath(normalized)
    if not normalized or "\x00" in normalized or pure.is_absolute():
        raise UpdaterError(f"Blocked unsafe symlink ZIP entry: {entry_name} -> {link_target}")

    resolved_target = link_path.parent.joinpath(*pure.parts).resolve(strict=False)
    root_resolved = root.resolve()
    try:
        resolved_target.relative_to(root_resolved)
    except ValueError as exc:
        raise UpdaterError(f"Blocked unsafe symlink ZIP entry: {entry_name} -> {link_target}") from exc
    return normalized


def zipinfo_mode(info: zipfile.ZipInfo) -> int:
    return (info.external_attr >> 16) & 0o777777


def extract_zip_safe(zip_path: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    pending_symlinks: list[tuple[Path, str, str]] = []
    with zipfile.ZipFile(zip_path) as archive:
        for info in archive.infolist():
            if not info.filename:
                continue
            target = safe_zip_target(dest, info.filename)
            mode = zipinfo_mode(info)
            if stat.S_ISLNK(mode):
                raw_target = archive.read(info)
                link_target = os.fsdecode(raw_target)
                pending_symlinks.append((target, safe_symlink_target(dest, target, link_target, info.filename), info.filename))
                continue
            if info.is_dir() or info.filename.endswith(("/", "\\")):
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
            if mode:
                try:
                    target.chmod(mode & 0o777)
                except OSError:
                    pass

        for target, link_target, entry_name in pending_symlinks:
            target.parent.mkdir(parents=True, exist_ok=True)
            if os.path.lexists(target):
                raise UpdaterError(f"Blocked duplicate symlink ZIP entry: {entry_name}")
            try:
                os.symlink(link_target, target)
            except (AttributeError, NotImplementedError, OSError) as exc:
                raise UpdaterError(f"Could not create symlink from ZIP entry: {entry_name} -> {link_target}\n{exc}") from exc


def unique_child_path(parent: Path, stem: str) -> Path:
    candidate = parent / stem
    if not candidate.exists():
        return candidate
    for index in range(2, 1000):
        candidate = parent / f"{stem}_{index}"
        if not candidate.exists():
            return candidate
    raise UpdaterError(f"Could not find a unique folder under {parent}.")


def expand_nested_zips(root: Path, max_depth: int = 3) -> None:
    seen: set[Path] = set()
    for _ in range(max_depth):
        nested = [path for path in root.rglob("*.zip") if path.is_file()]
        pending = [path for path in nested if path.resolve() not in seen]
        if not pending:
            return
        for zip_path in pending:
            seen.add(zip_path.resolve())
            target = unique_child_path(zip_path.parent, zip_path.stem)
            try:
                extract_zip_safe(zip_path, target)
            except (zipfile.BadZipFile, UpdaterError):
                pass


def glob_patterns(raw: str | None) -> list[str]:
    patterns = [part.strip() for part in str(raw or "").split(",") if part.strip()]
    return patterns or ["*"]


def launchable_priority(path: Path) -> int:
    name = path.name.lower()
    priority = 0
    if name in {"qss-m", "qss-m.exe", "qss-m.app", "quakespasm"}:
        priority += 100
    if "qss-m" in name:
        priority += 40
    if "qssm" in name:
        priority += 30
    if "quakespasm" in name:
        priority += 25
    if "qss" in name:
        priority += 20
    if any(part in name for part in ("linux", "l64", "x64", "64")):
        priority += 10
    return priority


def find_launchables(root: Path, launch_glob: str) -> list[Path]:
    patterns = glob_patterns(launch_glob)
    matches: list[Path] = []
    for path in root.rglob("*"):
        if path.is_dir() and path.suffix.lower() != ".app":
            continue
        if any(fnmatch.fnmatch(path.name, pattern) for pattern in patterns):
            matches.append(path)
    matches.sort(key=lambda path: (launchable_priority(path), path.stat().st_size if path.is_file() else 0, path.name.lower()), reverse=True)
    return matches


def single_child_dir(path: Path) -> Path:
    current = path
    for _ in range(5):
        children = [child for child in current.iterdir() if child.name != "__MACOSX"]
        dirs = [child for child in children if child.is_dir()]
        files = [child for child in children if child.is_file()]
        if len(dirs) == 1 and not files:
            current = dirs[0]
        else:
            break
    return current


def choose_payload_root(extract_root: Path, launch_glob: str, payload_root: str | None) -> dict:
    if payload_root:
        expanded = os.path.expandvars(os.path.expanduser(payload_root))
        candidate = Path(expanded)
        if not candidate.is_absolute():
            candidate = extract_root / candidate
        candidate = candidate.resolve()
        if not candidate.is_dir():
            raise UpdaterError(f"Requested payload root does not exist or is not a directory: {candidate}")
        return {"root": candidate, "launchables": find_launchables(candidate, launch_glob)}

    launchables = find_launchables(extract_root, launch_glob)
    if launchables:
        return {"root": launchables[0].parent, "launchables": launchables}

    top_children = [child for child in extract_root.iterdir() if child.name != "__MACOSX"]
    top_dirs = [child for child in top_children if child.is_dir()]
    top_files = [child for child in top_children if child.is_file() and child.suffix.lower() != ".zip"]
    if len(top_dirs) == 1 and not top_files:
        candidate = single_child_dir(top_dirs[0])
        return {"root": candidate, "launchables": find_launchables(candidate, launch_glob)}

    raise UpdaterError(
        f"Could not find package contents. No files matching '{launch_glob}' were found. "
        "Use --payload-root to point at the extracted package folder."
    )


def remove_path(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink()


def copy_symlink(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if os.path.lexists(target):
        remove_path(target)
    os.symlink(os.readlink(source), target)


def copy_path(source: Path, target: Path) -> None:
    if source.is_symlink():
        copy_symlink(source, target)
        return
    if source.is_dir():
        copy_directory_tree(source, target)
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_dir() and not target.is_symlink():
        shutil.rmtree(target)
    elif os.path.lexists(target):
        target.unlink()
    shutil.copy2(source, target)


def copy_directory_contents(source: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    for child in source.iterdir():
        target = dest / child.name
        if child.is_symlink():
            copy_symlink(child, target)
        elif child.is_dir():
            if os.path.lexists(target) and not (target.is_dir() and not target.is_symlink()):
                remove_path(target)
            copy_directory_contents(child, target)
        else:
            if target.is_dir() and not target.is_symlink():
                shutil.rmtree(target)
            elif os.path.lexists(target):
                target.unlink()
            shutil.copy2(child, target)


def copy_directory_tree(source: Path, dest: Path) -> None:
    if os.path.lexists(dest):
        remove_path(dest)
    shutil.copytree(source, dest, copy_function=shutil.copy2, symlinks=True)


def copy_payload_to_stage(payload_root: Path, stage_dir: Path) -> None:
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True, exist_ok=True)
    for child in payload_root.iterdir():
        if child.name == "__MACOSX":
            continue
        target = stage_dir / child.name
        if child.is_symlink():
            copy_symlink(child, target)
        elif child.is_dir():
            copy_directory_tree(child, target)
        else:
            shutil.copy2(child, target)


def ensure_launchables_executable(root: Path, launch_glob: str) -> None:
    for path in find_launchables(root, launch_glob):
        if path.is_file():
            mode = path.stat().st_mode
            path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def normalize_dir(path: Path) -> Path:
    return path.expanduser().resolve()


def dangerous_destination(dest: Path) -> bool:
    resolved = normalize_dir(dest)
    dangerous = {Path.home().resolve(), Path("/").resolve()}
    desktop = Path.home() / "Desktop"
    if desktop.exists():
        dangerous.add(desktop.resolve())
    return resolved in dangerous


def unique_backup_path(dest_dir: Path) -> Path:
    parent = dest_dir.parent
    leaf = dest_dir.name
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    base = parent / f"{leaf}.backup-{stamp}"
    if not base.exists():
        return base
    for index in range(2, 1000):
        candidate = parent / f"{leaf}.backup-{stamp}-{index}"
        if not candidate.exists():
            return candidate
    raise UpdaterError(f"Could not find a unique backup name for {dest_dir}.")


def unique_backup_zip_path(dest_dir: Path) -> Path:
    leaf = dest_dir.name
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    base = dest_dir / f"{leaf}.backup-{stamp}.zip"
    if not base.exists():
        return base
    for index in range(2, 1000):
        candidate = dest_dir / f"{leaf}.backup-{stamp}-{index}.zip"
        if not candidate.exists():
            return candidate
    raise UpdaterError(f"Could not find a unique backup ZIP name for {dest_dir}.")


def count_entries(path: Path) -> dict:
    files = 0
    dirs = 0
    for child in path.rglob("*"):
        if child.is_dir():
            dirs += 1
        else:
            files += 1
    return {"files": files, "dirs": dirs}


def copy_existing_path_to_backup_root(existing: Path, backup_root: Path, relative: PurePosixPath) -> int:
    if not os.path.lexists(existing):
        return 0
    backup_path = backup_root.joinpath(*relative.parts)
    if existing.is_symlink() or existing.is_file():
        copy_path(existing, backup_path)
        return 1
    if existing.is_dir():
        copy_directory_tree(existing, backup_path)
        return count_entries(backup_path)["files"]
    return 0


def add_matching_backup_items(source: Path, dest: Path, backup_root: Path, relative: PurePosixPath) -> int:
    if source.is_symlink() or not source.is_dir():
        return copy_existing_path_to_backup_root(dest, backup_root, relative)
    if dest.is_file() or dest.is_symlink():
        return copy_existing_path_to_backup_root(dest, backup_root, relative)
    if source.suffix.lower() == ".app":
        return copy_existing_path_to_backup_root(dest, backup_root, relative)
    if not dest.is_dir():
        return 0
    backed_up = 0
    for child in source.iterdir():
        child_relative = PurePosixPath(posixpath.join(str(relative), child.name))
        backed_up += add_matching_backup_items(child, dest / child.name, backup_root, child_relative)
    return backed_up


def write_zip_from_dir(source_dir: Path, zip_path: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in source_dir.rglob("*"):
                relative = path.relative_to(source_dir).as_posix()
                if path.is_symlink():
                    info = zipfile.ZipInfo(relative)
                    info.create_system = 3
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    archive.writestr(info, os.readlink(path))
                else:
                    archive.write(path, relative)
    except Exception:
        try:
            zip_path.unlink()
        except FileNotFoundError:
            pass
        raise


def selective_backup_zip(stage_dir: Path, dest_dir: Path) -> dict | None:
    if not dest_dir.exists():
        return None
    backup_path = unique_backup_zip_path(dest_dir)
    with tempfile.TemporaryDirectory(prefix="qssm_backup_") as backup_tmp:
        backup_root = Path(backup_tmp)
        backed_up = 0
        for child in stage_dir.iterdir():
            backed_up += add_matching_backup_items(child, dest_dir / child.name, backup_root, PurePosixPath(child.name))
        if backed_up <= 0:
            return None
        write_zip_from_dir(backup_root, backup_path)
        return {"path": backup_path, "files": backed_up}


def restore_selective_backup_zip(backup_zip: Path, dest_dir: Path) -> None:
    if not backup_zip.is_file():
        return
    with tempfile.TemporaryDirectory(prefix="qssm_restore_") as restore_tmp:
        restore_root = Path(restore_tmp)
        extract_zip_safe(backup_zip, restore_root)
        copy_stage_contents(restore_root, dest_dir)


def replace_directory(stage_dir: Path, dest_dir: Path, make_backup: bool) -> Path | None:
    dest = expand_path(dest_dir)
    if dangerous_destination(dest):
        raise UpdaterError(f"Refusing to replace overly broad destination: {dest}")
    if dest.exists() and not dest.is_dir():
        raise UpdaterError(f"Destination exists but is not a directory: {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)

    backup_path = None
    if dest.exists():
        if make_backup:
            backup_path = unique_backup_path(dest)
            dest.rename(backup_path)
        else:
            shutil.rmtree(dest)
    try:
        shutil.move(str(stage_dir), str(dest))
    except Exception:
        if backup_path and backup_path.exists() and not dest.exists():
            backup_path.rename(dest)
        raise
    return backup_path


def copy_stage_contents(stage_dir: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    for child in stage_dir.iterdir():
        target = dest_dir / child.name
        if child.is_symlink():
            copy_symlink(child, target)
        elif child.is_dir():
            if os.path.lexists(target) and (child.suffix.lower() == ".app" or target.is_file() or target.is_symlink()):
                remove_path(target)
            if child.suffix.lower() == ".app" or not target.exists():
                copy_directory_tree(child, target)
            else:
                copy_directory_contents(child, target)
        else:
            if target.is_dir() and not target.is_symlink():
                shutil.rmtree(target)
            elif os.path.lexists(target):
                target.unlink()
            shutil.copy2(child, target)


def merge_directory(stage_dir: Path, dest_dir: Path, make_backup: bool) -> Path | None:
    dest = expand_path(dest_dir)
    if dangerous_destination(dest):
        raise UpdaterError(f"Refusing to update overly broad destination: {dest}")
    if dest.exists() and not dest.is_dir():
        raise UpdaterError(f"Destination exists but is not a directory: {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)

    backup_info = None
    if dest.exists() and make_backup:
        ui_line("Backing up existing files that this release will overwrite...", "💾", "yellow")
        backup_info = selective_backup_zip(stage_dir, dest)
        if backup_info:
            ui_line(f"Backed up {backup_info['files']:,} existing file(s).", "✅", "green")
        else:
            ui_line("No existing release files needed backup.", "ℹ️", "dim")

    try:
        ui_line("Copying release files...", "📁", "cyan")
        copy_stage_contents(stage_dir, dest)
    except Exception:
        if backup_info and Path(backup_info["path"]).is_file():
            try:
                ui_line("Update failed; restoring backed-up files...", "⚠️", "yellow")
                restore_selective_backup_zip(Path(backup_info["path"]), dest)
            except Exception as exc:
                ui_line(f"Could not restore from backup ZIP: {exc}", "❌", "red")
        raise

    shutil.rmtree(stage_dir, ignore_errors=True)
    if backup_info:
        return Path(backup_info["path"])
    return None


def deploy_stage(stage_dir: Path, dest_dir: Path, make_backup: bool, mirror: bool) -> Path | None:
    if mirror:
        return replace_directory(stage_dir, dest_dir, make_backup)
    return merge_directory(stage_dir, dest_dir, make_backup)


def effective_options(args: argparse.Namespace, config: dict) -> dict:
    platform_key = normalize_platform(first_non_empty(args.platform, get_config_value(config, "platform"), default_platform()))
    preset = PLATFORM_PRESETS[platform_key]
    launch_glob = first_non_empty(args.launch_glob, args.exe_glob, get_config_value(config, "launch_glob"), preset["launch_glob"])
    artifact_hint = args.artifact_name_hint
    if artifact_hint is None:
        artifact_hint = first_non_empty(get_config_value(config, "artifact_name_hint"), preset["artifact_name_hint"])
    return {
        "platform": platform_key,
        "owner": first_non_empty(args.owner, get_config_value(config, "owner"), DEFAULT_OWNER),
        "repo": first_non_empty(args.repo, get_config_value(config, "repo"), DEFAULT_REPO),
        "workflow_name": first_non_empty(args.workflow_name, get_config_value(config, "workflow_name"), preset["workflow_name"]),
        "job_name": first_non_empty(args.job_name, get_config_value(config, "job_name"), preset["job_name"]),
        "artifact_name_hint": artifact_hint,
        "dest": expand_path(first_non_empty(args.dest, get_config_value(config, "dest"), default_dest())),
        "launch_glob": launch_glob,
        "payload_root": first_non_empty(args.payload_root, get_config_value(config, "payload_root")),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Download latest QSS-M CI artifact and update a local QSS-M folder.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--setup", "-setup", action="store_true", help="Run first-time setup again and save config.")
    parser.add_argument("--config", help="Override config file path.")
    parser.add_argument("--no-config", action="store_true", help="Ignore saved config and do not save prompted values.")
    parser.add_argument("--no-prompt", action="store_true", help="Fail or use defaults instead of prompting.")
    parser.add_argument("--platform", choices=["linux", "macos", "windows"], help="Artifact platform preset.")
    parser.add_argument("--owner")
    parser.add_argument("--repo")
    parser.add_argument("--workflow-name")
    parser.add_argument("--job-name")
    parser.add_argument("--branch", help="Defaults to repo default branch.")
    parser.add_argument("--dest")
    parser.add_argument("--token", help="Overrides saved config, GITHUB_TOKEN, and GitHub CLI auth.")
    parser.add_argument("--artifact-name-hint")
    parser.add_argument("--include-logs", action="store_true", help="Allow artifacts with 'log' in the name.")
    parser.add_argument("--launch-glob", help="Comma-separated launchable filename glob(s).")
    parser.add_argument("--payload-root", help="Optional extracted subfolder to deploy.")
    parser.add_argument("--allow-older", action="store_true", help="Use newest older success if latest commit has no success.")
    parser.add_argument("--dry-run", action="store_true", help="Download and inspect, but do not update the destination.")
    parser.add_argument("--keep-temp", action="store_true", help="Keep downloaded/extracted temp files for debugging.")
    parser.add_argument("--no-backup", action="store_true", help="Skip the backup prompt and do not back up before updating.")
    parser.add_argument("--mirror", action="store_true", help="Replace the destination folder exactly instead of merging.")
    parser.add_argument("--exe-glob", help=argparse.SUPPRESS)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    temp_root: Path | None = None
    try:
        prompt_allowed = can_prompt(args)
        if args.setup and args.no_config:
            raise UpdaterError("--setup cannot be combined with --no-config.")

        config_path = expand_path(args.config) if args.config else default_config_path()
        config = {} if args.no_config else load_config(config_path)
        if args.setup or ((not args.no_config) and not get_config_value(config, "dest")):
            config = interactive_setup(args, config, config_path)

        opts = effective_options(args, config)
        token_info = resolve_token(args, config, prompt_allowed)
        if token_info["source"] == "interactive prompt" and token_info["token"] and not args.no_config:
            if prompt_yes_no("Save this token for future runs", True):
                config["token"] = token_info["token"]
                save_config(config_path, config)

        owner = str(opts["owner"])
        repo = str(opts["repo"])
        dest_dir = Path(opts["dest"])

        temp_root = Path(tempfile.mkdtemp(prefix="qssm_artifact_"))
        ui_section("QSS-M update", "🚀")
        ui_kv("Repo", f"{owner}/{repo}", "🔗")
        ui_kv("Platform", f"{opts['platform']} ({PLATFORM_PRESETS[opts['platform']]['label']})", "🖥️")
        ui_kv("Workflow", opts["workflow_name"], "⚙️")
        ui_kv("Required job", opts["job_name"], "✅")
        ui_kv("Event", "push", "📌")
        ui_kv("Auth", token_info["source"], "🔑")
        if not args.no_config:
            ui_kv("Config", config_path, "📄")

        branch = args.branch or default_branch(owner, repo, token_info["token"])
        head_sha = branch_head_sha(owner, repo, branch, token_info["token"])
        ui_section("GitHub Actions", "🔎")
        ui_kv("Branch", branch, "🌿")
        ui_kv("Latest commit", head_sha[:12], "🧬")

        run = find_matching_run(owner, repo, branch, head_sha, opts["workflow_name"], token_info["token"], args.allow_older)
        run_id = int(run["id"])
        run_sha = str(run.get("head_sha") or "")
        ui_kv("Using run", f"#{run.get('run_number')} id={run_id} sha={run_sha[:12]}", "🏃")
        if run.get("html_url"):
            ui_kv("Run URL", run["html_url"], "🔗")

        job = confirm_job_success(owner, repo, run_id, opts["job_name"], token_info["token"])
        ui_line(f"Confirmed job success: {job.get('name')}", "✅", "green")

        artifacts = get_artifacts(
            owner,
            repo,
            run_id,
            token_info["token"],
            opts["job_name"],
            opts["artifact_name_hint"],
            args.include_logs,
        )
        ui_section("Artifact", "📦")
        ui_line("Artifact selected:", "✅", "green")
        for artifact in artifacts:
            ui_line(f"{artifact.get('name')} ({int(artifact.get('size_in_bytes') or 0):,} bytes)", "•")

        extract_root = temp_root / "extracted"
        extract_root.mkdir(parents=True, exist_ok=True)
        for artifact in artifacts:
            artifact_id = artifact.get("id")
            artifact_name = str(artifact.get("name") or f"artifact-{artifact_id}")
            archive_url = artifact.get("archive_download_url")
            if not archive_url:
                continue
            zip_path = temp_root / f"{artifact_name}-{artifact_id}.zip"
            artifact_extract_dir = extract_root / artifact_name
            ui_line(f"Downloading artifact: {artifact_name}", "⬇️", "cyan")
            download_github_artifact(
                str(archive_url),
                zip_path,
                token_info["token"],
                config_path,
                artifact_name,
                int(artifact.get("size_in_bytes") or 0),
                artifact.get("digest"),
            )
            extract_zip_safe(zip_path, artifact_extract_dir)

        expand_nested_zips(extract_root)

        choice = choose_payload_root(extract_root, opts["launch_glob"], opts["payload_root"])
        payload_root = Path(choice["root"])
        launchables = list(choice["launchables"])
        counts = count_entries(payload_root)

        ui_section("Payload", "📁")
        ui_kv("Root", payload_root, "📂")
        ui_kv("Contents", f"{counts['files']:,} files, {counts['dirs']:,} directories", "📊")
        if launchables:
            ui_line("Launchable candidates:", "🚀", "green")
            for path in launchables[:5]:
                try:
                    relative = path.relative_to(extract_root)
                except ValueError:
                    relative = path
                ui_line(relative, "•")

        stage_dir = temp_root / "stage"
        copy_payload_to_stage(payload_root, stage_dir)
        ensure_launchables_executable(stage_dir, opts["launch_glob"])

        make_backup = False
        backup_target_exists = dest_dir.exists()
        if args.dry_run:
            action = "mirror-replace" if args.mirror else "update"
            ui_section("Dry run", "🧪")
            ui_line(f"Would {action}: {dest_dir}", "🧪", "yellow")
            if backup_target_exists and not args.no_backup:
                if prompt_allowed:
                    if args.mirror:
                        ui_line(f"Would ask whether to back up current folder as: {unique_backup_path(dest_dir)}", "💾", "yellow")
                    else:
                        ui_line(f"Would ask whether to create backup ZIP for overwritten release files: {unique_backup_zip_path(dest_dir)}", "💾", "yellow")
                else:
                    ui_line("Would skip backup; prompts are disabled and the default is No.", "ℹ️", "dim")
            return 0

        if backup_target_exists and not args.no_backup:
            if prompt_allowed:
                if args.mirror:
                    backup_prompt = "Back up current folder before replacing it"
                else:
                    backup_prompt = "Back up current files this release will overwrite"
                make_backup = prompt_yes_no(backup_prompt, False)
            else:
                ui_line("Backup skipped; prompts are disabled and the default is No.", "ℹ️", "dim")

        ui_section("Deploy", "🚚")
        ui_line(f"{'Mirror replacing' if args.mirror else 'Updating'}: {dest_dir}", "🚚", "cyan")
        backup_path = deploy_stage(stage_dir, dest_dir, make_backup, args.mirror)
        if backup_path:
            ui_line(f"Backup: {backup_path}", "💾", "yellow")

        print()
        ui_line("Done.", "✅", "green")
        return 0
    except UpdaterError as exc:
        eprint()
        ui_error(f"ERROR: {exc}")
        return 1
    except KeyboardInterrupt:
        eprint()
        ui_error("ERROR: Interrupted.")
        return 130
    finally:
        if temp_root:
            if args.keep_temp:
                print()
                ui_line(f"Temp files kept at: {temp_root}", "🧰", "yellow")
            else:
                shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
__QSSM_UPDATER_PYTHON_PAYLOAD_BELOW__
)
exec "$PYTHON_BIN" -c "$PYTHON_PAYLOAD" "$@"
