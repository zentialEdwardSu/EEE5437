from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent
SOURCE_DIR = REPO_ROOT / "code"
DEFAULT_BUILD_DIR = REPO_ROOT / "build"
LLVM_BIN_DIR = Path(r"C:\Program Files\LLVM\bin")
VISUAL_STUDIO_GENERATORS = (
    "Visual Studio 18 2026",
    "Visual Studio 17 2022",
    "Visual Studio 16 2019",
)


def run(command: list[str], cwd: Path = REPO_ROOT) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def capture(command: list[str], cwd: Path = REPO_ROOT) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout


def find_vswhere() -> Path | None:
    from_path = shutil.which("vswhere")
    if from_path:
        return Path(from_path)

    program_files_x86 = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    bundled = program_files_x86 / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if bundled.exists():
        return bundled
    return None


def find_standalone_clang_cl() -> Path | None:
    on_path = shutil.which("clang-cl")
    if on_path:
        return Path(on_path)

    bundled = LLVM_BIN_DIR / "clang-cl.exe"
    if bundled.exists():
        return bundled
    return None


def find_lld_link() -> Path | None:
    on_path = shutil.which("lld-link")
    if on_path:
        return Path(on_path)

    bundled = LLVM_BIN_DIR / "lld-link.exe"
    if bundled.exists():
        return bundled
    return None


def find_ninja() -> Path | None:
    on_path = shutil.which("ninja")
    if on_path:
        return Path(on_path)
    return None


def find_nmake() -> Path | None:
    on_path = shutil.which("nmake")
    if on_path:
        return Path(on_path)

    vswhere = find_vswhere()
    if not vswhere:
        return None

    installation_path = capture(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"]
    ).strip()
    if not installation_path:
        return None

    msvc_root = Path(installation_path) / "VC" / "Tools" / "MSVC"
    if not msvc_root.exists():
        return None

    candidates = sorted(msvc_root.glob(r"*\bin\Hostx64\x64\nmake.exe"), reverse=True)
    if candidates:
        return candidates[0]
    return None


def has_visual_studio_clangcl() -> bool:
    vswhere = find_vswhere()
    if not vswhere:
        return False

    installation_path = capture(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"]
    ).strip()
    if not installation_path:
        return False

    clang_cl = Path(installation_path) / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-cl.exe"
    return clang_cl.exists()


def resolve_generator() -> tuple[str, list[str]]:
    configured_generator = os.environ.get("CMAKE_GENERATOR")
    if configured_generator:
        extra_args: list[str] = []
        toolset = os.environ.get("CMAKE_GENERATOR_TOOLSET")
        if toolset:
            extra_args.extend(["-T", toolset])
        platform = os.environ.get("CMAKE_GENERATOR_PLATFORM")
        if platform:
            extra_args.extend(["-A", platform])
        return configured_generator, extra_args

    clang_cl = find_standalone_clang_cl()
    common_clang_args: list[str] = []
    if clang_cl:
        common_clang_args.extend(
            [
                f"-DCMAKE_C_COMPILER={clang_cl}",
                f"-DCMAKE_CXX_COMPILER={clang_cl}",
            ]
        )

    ninja = find_ninja()
    if ninja and clang_cl:
        return "Ninja", [f"-DCMAKE_MAKE_PROGRAM={ninja}", *common_clang_args]

    nmake = find_nmake()
    if nmake and clang_cl:
        return "NMake Makefiles", [f"-DCMAKE_MAKE_PROGRAM={nmake}", *common_clang_args]

    cmake_help = capture(["cmake", "--help"])
    for generator in VISUAL_STUDIO_GENERATORS:
        if generator in cmake_help:
            if not has_visual_studio_clangcl():
                raise RuntimeError(
                    "A Visual Studio generator is available, but the Visual Studio ClangCL toolset was not found. "
                    "Install the Visual Studio LLVM/Clang components, or install Ninja so standalone clang-cl can be used."
                )
            return generator, ["-T", "ClangCL", "-A", "x64"]

    raise RuntimeError(
        "No usable clang-cl backend was detected. "
        "Install standalone clang-cl plus Ninja or NMake, or install a Visual Studio generator that supports ClangCL."
    )


def ensure_configured(build_dir: Path, config: str) -> None:
    cache_file = build_dir / "CMakeCache.txt"
    if cache_file.exists():
        return
    configure(build_dir, config)


def configure(build_dir: Path, config: str) -> None:
    generator, extra_args = resolve_generator()
    build_dir.mkdir(parents=True, exist_ok=True)

    command = [
        "cmake",
        "-S",
        str(SOURCE_DIR),
        "-B",
        str(build_dir),
        "-G",
        generator,
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]

    if "Visual Studio" not in generator:
        command.append(f"-DCMAKE_BUILD_TYPE={config}")

    command.extend(extra_args)
    run(command)


def discover_homeworks() -> list[str]:
    if not SOURCE_DIR.exists():
        return []

    homeworks: list[str] = []
    for entry in sorted(SOURCE_DIR.iterdir()):
        if entry.is_dir() and entry.name.startswith("hw"):
            homeworks.append(entry.name)
    return homeworks


def discover_library_targets() -> list[str]:
    lib_dir = SOURCE_DIR / "lib"
    if not lib_dir.exists():
        return []

    def has_sources(directory: Path) -> bool:
        for candidate in directory.rglob("*"):
            if candidate.suffix.lower() in {".c", ".cc", ".cpp", ".cxx"}:
                return True
        return False

    libraries: list[str] = []
    for entry in sorted(lib_dir.iterdir()):
        if entry.name in {"internal", "vendor"}:
            continue
        if entry.is_dir():
            if has_sources(entry):
                libraries.append(f"lib_{entry.name}")
        elif entry.suffix.lower() in {".c", ".cc", ".cpp", ".cxx"}:
            libraries.append(f"lib_{entry.stem}")

    internal_dir = lib_dir / "internal"
    if internal_dir.exists():
        for entry in sorted(internal_dir.iterdir()):
            if entry.is_dir():
                if has_sources(entry):
                    libraries.append(f"lib_{entry.name}")
            elif entry.suffix.lower() in {".c", ".cc", ".cpp", ".cxx"}:
                libraries.append(f"lib_{entry.stem}")
    return libraries


def discover_test_targets(scope: str | None) -> list[str]:
    tests_dir = SOURCE_DIR / "tests"
    if not tests_dir.exists():
        return []

    matching_targets: list[str] = []
    for source_file in sorted(tests_dir.rglob("*")):
        if source_file.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx"}:
            continue
        stem = source_file.stem
        if scope is None or stem.startswith(f"{scope}_test_"):
            matching_targets.append(stem)

    return matching_targets


def print_target_list(scope: str | None) -> None:
    homeworks = discover_homeworks()
    libraries = discover_library_targets()
    tests = discover_test_targets(scope)

    if scope is not None:
        homeworks = [name for name in homeworks if name == scope]
        libraries = [name for name in libraries if name == scope]

    print("Homeworks:")
    if homeworks:
        for name in homeworks:
            print(f"  {name}")
    else:
        print("  (none)")

    print("Libraries:")
    if libraries:
        for name in libraries:
            print(f"  {name}")
    else:
        print("  (none)")

    print("Tests:")
    if tests:
        for name in tests:
            print(f"  {name}")
    else:
        print("  (none)")


def build_selected_targets(build_dir: Path, config: str, jobs: int | None, targets: list[str]) -> None:
    ensure_configured(build_dir, config)

    command = [
        "cmake",
        "--build",
        str(build_dir),
        "--config",
        config,
        "--target",
        *targets,
    ]
    if jobs:
        command.extend(["--parallel", str(jobs)])
    run(command)


def build(build_dir: Path, config: str, jobs: int | None, scope: str | None = None) -> None:
    build_targets = ["homeworks", "project_tests"]
    if scope is not None:
        build_targets = [scope]
    build_selected_targets(build_dir, config, jobs, build_targets)


def find_executable(build_dir: Path, config: str, scope: str) -> Path | None:
    candidates = [
        build_dir / "bin" / f"{scope}.exe",
        build_dir / "bin" / config / f"{scope}.exe",
        build_dir / config / f"{scope}.exe",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    for candidate in build_dir.rglob(f"{scope}.exe"):
        if candidate.is_file():
            return candidate

    return None


def run_target(build_dir: Path, config: str, jobs: int | None, scope: str, program_args: list[str]) -> None:
    build(build_dir, config, jobs, scope)

    executable = find_executable(build_dir, config, scope)
    if executable is None:
        raise RuntimeError(f"Target {scope} did not produce a runnable executable.")

    run([str(executable), *program_args], cwd=REPO_ROOT)


def test(build_dir: Path, config: str, scope: str | None = None) -> None:
    ensure_configured(build_dir, config)

    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "-C",
        config,
        "--output-on-failure",
    ]
    if scope is not None:
        command.extend(["-R", f"^{re.escape(scope)}_test_"])
    run(command)


def clean(build_dir: Path) -> None:
    if build_dir.exists():
        shutil.rmtree(build_dir)
        print(f"Removed {build_dir}")
    else:
        print(f"Build directory does not exist: {build_dir}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure, build, and test all homeworks in the code/ project."
    )
    parser.add_argument(
        "action",
        nargs="?",
        choices=("all", "configure", "build", "test", "run", "list", "clean"),
        default="all",
    )
    parser.add_argument(
        "scope",
        nargs="?",
        help="Optional target scope such as hw1 or lib_some_lib.",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="CMake build directory (default: ./build).",
    )
    parser.add_argument(
        "--config",
        default="Debug",
        help="Build configuration, e.g. Debug or Release.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=None,
        help="Parallel build job count.",
    )
    parser.add_argument(
        "program_args",
        nargs=argparse.REMAINDER,
        help="Arguments passed to `run <scope>` after `--`.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    scoped_test_targets = discover_test_targets(args.scope)

    try:
        if args.action == "configure":
            configure(build_dir, args.config)
        elif args.action == "build":
            build(build_dir, args.config, args.jobs, args.scope)
        elif args.action == "test":
            if args.scope:
                targets_to_build = [args.scope, *scoped_test_targets]
                build_selected_targets(build_dir, args.config, args.jobs, targets_to_build)
            else:
                build(build_dir, args.config, args.jobs)
            if args.scope and not scoped_test_targets:
                print(f"No tests matched scope: {args.scope}")
                return 0
            test(build_dir, args.config, args.scope)
        elif args.action == "run":
            if not args.scope:
                raise RuntimeError("`run` requires a scope such as hw1.")
            program_args = args.program_args
            if program_args and program_args[0] == "--":
                program_args = program_args[1:]
            run_target(build_dir, args.config, args.jobs, args.scope, program_args)
        elif args.action == "list":
            print_target_list(args.scope)
        elif args.action == "clean":
            clean(build_dir)
        else:
            configure(build_dir, args.config)
            if args.scope:
                targets_to_build = [args.scope, *scoped_test_targets]
                build_selected_targets(build_dir, args.config, args.jobs, targets_to_build)
            else:
                build(build_dir, args.config, args.jobs)
            if args.scope and not scoped_test_targets:
                print(f"No tests matched scope: {args.scope}")
                return 0
            test(build_dir, args.config, args.scope)
    except subprocess.CalledProcessError as exc:
        return exc.returncode
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
