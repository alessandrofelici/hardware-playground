#!/usr/bin/env python3
"""fw -- build, flash and switch between this repo's FreeWili OG apps.

  fw list                     apps in apps/; * marks the current one
  fw use <app>                make <app> current (VS Code's Compile/Run use it)
  fw build [app]              configure, then build both halves of apps/<app>/
  fw flash [app]              flash apps/<app>'s main UF2 (carries the display)
  fw run [app]                build, then flash
  fw new-app <name>           copy tools/app_template to apps/<name>/
  fw stock                    download, verify and flash stock FreeWili OG firmware
  fw console [main|display]   attach to a CPU's USB console (default: main)
  fw bootsel main|display     reboot one CPU into BOOTSEL

[app] defaults to the current app. --print shows what build/flash/run/stock
would do instead of doing it.

Flashing, CPU identification, the console and BOOTSEL are the BSP's own
external/wiliOGbsp/tools/fw.py. It only ever flashes the MAIN CPU: the
display half of an app travels inside the main UF2 and is pushed to the
display CPU over the inter-CPU link by the OG display bootloader.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import subprocess
import sys
import urllib.request

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
APPS_DIR = REPO_ROOT / "apps"
TEMPLATE_DIR = REPO_ROOT / "tools" / "app_template"
BUILD_DIR = REPO_ROOT / "build"
CURRENT_FILE = REPO_ROOT / ".fw-app"
TASKS_JSON = REPO_ROOT / ".vscode" / "tasks.json"
STOCK_DIR = REPO_ROOT / "firmware" / "stock"
BSP_FW = REPO_ROOT / "external" / "wiliOGbsp" / "tools" / "fw.py"

STOCK_RELEASE_API = (
    "https://api.github.com/repos/freewili/freewili-firmware/releases/latest")
STOCK_UF2_RE = re.compile(r"ogfw_main-\d+\.uf2")
STOCK_SUMS_RE = re.compile(r"checksums-.+\.txt")

APP_NAME_RE = re.compile(r"[a-z][a-z0-9_]*")
APP_DECL_RE = re.compile(
    r'fwog_(display|main)_app\(\s*\w+\s+VERSION\s+(\d{3})\s+'
    r'DESCRIPTION\s+"([^"]*)"', re.S)
# fw.py's own product-string prefixes (contract rule 5 in the BSP's AGENTS.md).
CONSOLE_PRODUCT = {"main": "FWOG main ", "display": "FWOG display "}


def load_bsp():
    """The BSP's fw.py as a module, loaded by path: it is also named fw.py,
    so a plain import would find this file instead."""
    if not BSP_FW.is_file():
        sys.exit("fw: the FreeWili OG BSP is missing from external/wiliOGbsp. "
                 "Run: git submodule update --init")
    spec = importlib.util.spec_from_file_location("wiliog_fw", BSP_FW)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---- apps -----------------------------------------------------------------

def app_names():
    if not APPS_DIR.is_dir():
        return []
    return sorted(d.name for d in APPS_DIR.iterdir()
                  if (d / "CMakeLists.txt").is_file())


def current_app():
    names = app_names()
    try:
        name = CURRENT_FILE.read_text().strip()
    except OSError:
        name = ""
    if name in names:
        return name
    if "hello" in names:
        return "hello"
    return names[0] if names else None


def resolve_app(app):
    """The app folder to act on, or exit with the list of what exists."""
    if app is None:
        app = current_app()
        if app is None:
            sys.exit("fw: no apps in apps/ -- create one with `fw new-app <name>`")
    for suffix in ("_display", "_main"):
        if app.endswith(suffix):
            app = app[: -len(suffix)]
    if app not in app_names():
        sys.exit(f"fw: no app named {app!r}. Apps: {', '.join(app_names()) or 'none'}")
    return app


def uf2_for(app):
    return BUILD_DIR / "apps" / app / f"{app}_main.uf2"


def app_info(app):
    """{cpu: (version, description)} parsed from apps/<app>/CMakeLists.txt."""
    text = (APPS_DIR / app / "CMakeLists.txt").read_text()
    return {cpu: (ver, desc) for cpu, ver, desc in APP_DECL_RE.findall(text)}


# ---- build ----------------------------------------------------------------

def run_cmds(cmds, do_print):
    for cmd in cmds:
        print("> " + " ".join(str(c) for c in cmd), flush=True)
        if do_print:
            continue
        try:
            rc = subprocess.run(cmd, cwd=REPO_ROOT).returncode
        except FileNotFoundError:
            sys.exit(f"fw: `{cmd[0]}` not found. Install the toolchain with the "
                     "Raspberry Pi Pico VS Code extension (it puts CMake under "
                     "~/.pico-sdk/cmake), or put CMake 3.21+ on PATH.")
        if rc != 0:
            return rc
    return 0


def do_build(bsp, app, do_print):
    cmake = bsp.cmake_tool()
    preset = bsp.preset_name()
    # Configure every time: it picks up new app folders and is cheap when
    # nothing changed. Python3_EXECUTABLE pins the interpreter running this
    # script, which is the one known to work.
    configure = [cmake, "--preset", preset,
                 f"-DPython3_EXECUTABLE={pathlib.Path(sys.executable).as_posix()}"]
    build = [cmake, "--build", "--preset", preset, "--target", f"{app}_main"]
    rc = run_cmds([configure, build], do_print)
    if rc == 0 and not do_print:
        print(f"built {uf2_for(app).relative_to(REPO_ROOT)}")
    return rc


def do_flash(bsp, app, do_print):
    uf2 = uf2_for(app)
    if not do_print and not uf2.exists():
        print(f"fw flash: {uf2.relative_to(REPO_ROOT)} not found -- run `fw build {app}`",
              file=sys.stderr)
        return 1
    return bsp.do_flash(app, do_print, uf2_override=str(uf2))


# ---- new-app --------------------------------------------------------------

def new_app(name):
    if not APP_NAME_RE.fullmatch(name) or name == "template":
        raise ValueError(f"{name!r} is not a usable app name: lowercase letters, "
                         "digits and underscores, starting with a letter")
    if name.endswith(("_display", "_main")):
        raise ValueError("give the bare name; the _display/_main targets are made for you")
    dest = APPS_DIR / name
    if dest.exists():
        raise FileExistsError(f"apps/{name} already exists")
    shutil.copytree(TEMPLATE_DIR, dest)
    for f in [dest / "CMakeLists.txt", *sorted(dest.glob("*/*.c"))]:
        text = f.read_text()
        for old, new in (("template_", f"{name}_"), ("template:", f"{name}:"),
                         ("/* template --", f"/* {name} --")):
            text = text.replace(old, new)
        f.write_text(text)
    return dest


def sync_vscode_app_list():
    """Keep the app picker in .vscode/tasks.json in step with apps/."""
    try:
        tasks = json.loads(TASKS_JSON.read_text())
    except (OSError, ValueError) as e:
        print(f"note: could not update {TASKS_JSON.name} ({e})")
        return
    for inp in tasks.get("inputs", []):
        if inp.get("id") == "app":
            inp["options"] = app_names()
    TASKS_JSON.write_text(json.dumps(tasks, indent=2) + "\n")


# ---- stock firmware -------------------------------------------------------

def _get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "hardware-playground-fw"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch_stock():
    """Download the latest stock MAIN UF2 into firmware/stock/, verified
    against the release's checksum file. Reuses a verified cached copy."""
    release = json.loads(_get(STOCK_RELEASE_API))
    assets = {a["name"]: a["browser_download_url"] for a in release.get("assets", [])}
    uf2_name = next((n for n in assets if STOCK_UF2_RE.fullmatch(n)), None)
    sums_name = next((n for n in assets if STOCK_SUMS_RE.fullmatch(n)), None)
    if uf2_name is None or sums_name is None:
        raise ValueError(f"release {release.get('tag_name')} has no MAIN UF2 or checksum file")
    sums = {}
    for line in _get(assets[sums_name]).decode().splitlines():
        parts = line.split()
        if len(parts) == 2:
            sums[parts[1].lstrip("*")] = parts[0].lower()
    want = sums.get(uf2_name)
    if want is None:
        raise ValueError(f"{sums_name} has no checksum for {uf2_name}")

    STOCK_DIR.mkdir(parents=True, exist_ok=True)
    dest = STOCK_DIR / uf2_name
    if dest.exists() and _sha256(dest) == want:
        print(f"stock firmware {release.get('tag_name')}: using cached {dest.name}")
        return dest
    print(f"downloading stock firmware {release.get('tag_name')}: {uf2_name} ...")
    tmp = dest.with_suffix(".part")
    tmp.write_bytes(_get(assets[uf2_name]))
    got = _sha256(tmp)
    if got != want:
        tmp.unlink()
        raise ValueError(f"{uf2_name} checksum mismatch: got {got}, expected {want}")
    tmp.replace(dest)
    return dest


def do_stock(bsp, do_print):
    if do_print:
        print(f"download the latest {STOCK_UF2_RE.pattern} from {STOCK_RELEASE_API}, "
              "verify its SHA-256, then flash it to the MAIN CPU")
        return 0
    try:
        uf2 = fetch_stock()
    except (OSError, ValueError) as e:
        cached = sorted(STOCK_DIR.glob("ogfw_main-*.uf2"))
        if not cached:
            print(f"fw stock: {e}", file=sys.stderr)
            return 1
        uf2 = cached[-1]
        print(f"note: could not check for a newer release ({e}); using {uf2.name}")
    return bsp.do_flash("stock", do_print, uf2_override=str(uf2))


# ---- cli ------------------------------------------------------------------

def main(argv=None):
    p = argparse.ArgumentParser(prog="fw", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list")
    u = sub.add_parser("use")
    u.add_argument("app")
    for name in ("build", "flash", "run"):
        s = sub.add_parser(name)
        s.add_argument("app", nargs="?")
        s.add_argument("--print", action="store_true", dest="do_print")
    n = sub.add_parser("new-app")
    n.add_argument("name")
    st = sub.add_parser("stock")
    st.add_argument("--print", action="store_true", dest="do_print")
    c = sub.add_parser("console")
    c.add_argument("cpu", nargs="?", choices=sorted(CONSOLE_PRODUCT), default="main")
    c.add_argument("--port")
    b = sub.add_parser("bootsel")
    b.add_argument("cpu", choices=sorted(CONSOLE_PRODUCT))

    a = p.parse_args(argv)

    if a.cmd == "list":
        cur = current_app()
        for name in app_names():
            info = app_info(name)
            ver = info.get("main", info.get("display", ("???", "")))[0]
            desc = info.get("display", info.get("main", ("", "")))[1]
            state = "built" if uf2_for(name).exists() else "-"
            print(f"{'*' if name == cur else ' '} {name:<16} v{ver}  {state:<5}  {desc}")
        return 0
    if a.cmd == "use":
        app = resolve_app(a.app)
        CURRENT_FILE.write_text(app + "\n")
        print(f"current app: {app}")
        return 0
    if a.cmd == "new-app":
        try:
            dest = new_app(a.name)
        except (ValueError, FileExistsError) as e:
            print(f"fw new-app: {e}", file=sys.stderr)
            return 2
        CURRENT_FILE.write_text(a.name + "\n")
        sync_vscode_app_list()
        print(f"created {dest.relative_to(REPO_ROOT)} (now the current app)\n"
              f"next: edit its VERSION/DESCRIPTION in CMakeLists.txt, then `fw run`")
        return 0

    bsp = load_bsp()
    if a.cmd == "build":
        return do_build(bsp, resolve_app(a.app), a.do_print)
    if a.cmd == "flash":
        return do_flash(bsp, resolve_app(a.app), a.do_print)
    if a.cmd == "run":
        app = resolve_app(a.app)
        rc = do_build(bsp, app, a.do_print)
        return rc if rc != 0 else do_flash(bsp, app, a.do_print)
    if a.cmd == "stock":
        return do_stock(bsp, a.do_print)
    if a.cmd == "console":
        return bsp.do_console(a.port, CONSOLE_PRODUCT[a.cpu])
    if a.cmd == "bootsel":
        return bsp.do_bootsel(cpu=a.cpu)
    return 1


if __name__ == "__main__":
    sys.exit(main())
