# SPDX-License-Identifier: Apache-2.0
"""Probe source identity with original temporary Git/CMake fixtures, not C++ builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MARKER = """meshvale-project: meshvale-interchange
node: $Format:%H$
node-date: $Format:%cI$
describe-name: $Format:%(describe:tags=true,match=v[0-9]*)$
ref-names: $Format:%D$
"""
ATTRIBUTES = ".git_archival.txt export-subst\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--git", default="git")
    parser.add_argument("--output", type=Path, help="New directory for receipt.json")
    args = parser.parse_args()
    results = []
    commands = []
    if args.output:
        assert not args.output.exists(), args.output
        args.output.mkdir(parents=True)

    def save_partial(pending=None):
        if args.output:
            (args.output / "receipt.json").write_text(json.dumps({"completed": False, "pending_case": pending, "case_count": len(results), "cases": results, "commands": commands}, indent=2) + "\n", encoding="utf-8")

    save_partial("tool-discovery")
    cmake, git = shutil.which(args.cmake), shutil.which(args.git)
    if not cmake or not git:
        parser.error("CMake 3.24+ and Git are required for the identity probe")
    assert (ROOT / ".git_archival.txt").read_text(encoding="utf-8") == MARKER
    assert (ROOT / ".gitattributes").read_text(encoding="utf-8") == ATTRIBUTES
    environment = os.environ.copy()
    for key in list(environment):
        if key in {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE"}:
            del environment[key]

    def record_command(command, process):
        ordinal = len(commands)
        stderr_name = f"command-{ordinal:03d}.stderr"
        if args.output:
            (args.output / stderr_name).write_bytes(process.stderr)
        commands.append({"argv": command, "actual_exit": process.returncode,
                         "stderr": stderr_name if args.output else None,
                         "stderr_sha256": hashlib.sha256(process.stderr).hexdigest(),
                         "stdout_sha256": hashlib.sha256(process.stdout).hexdigest(),
                         "stdout_retained": False})

    def run(command, cwd=None):
        completed = subprocess.run(command, cwd=cwd, env=environment, capture_output=True)
        record_command(command, completed)
        save_partial("fixture-command")
        assert completed.returncode == 0, (command, completed.stdout.decode(errors="replace"),
                                            completed.stderr.decode(errors="replace"))
        return completed.stdout

    with tempfile.TemporaryDirectory(prefix="meshvale-interchange-version-") as directory:
        temporary = Path(directory).resolve()
        assert temporary.is_dir() and temporary.is_absolute()

        def source(name, info=None, marker=MARKER):
            path = temporary / name
            path.mkdir(parents=True)
            (path / "cmake").mkdir()
            shutil.copyfile(ROOT / "cmake/SourceVersion.cmake", path / "cmake/SourceVersion.cmake")
            if marker is not None:
                (path / ".git_archival.txt").write_text(marker, encoding="utf-8", newline="\n")
            (path / ".gitattributes").write_text(ATTRIBUTES, encoding="utf-8", newline="\n")
            (path / ".gitignore").write_text(".ignored/\n", encoding="utf-8")
            (path / "README").write_text("original fixture\n", encoding="utf-8")
            if info is not None:
                (path / "PKG-INFO").write_bytes(info.encode("utf-8"))
            return path

        def repository(path):
            run([git, "-c", "init.defaultBranch=main", "init", "--template=", "-q", str(path)])
            for key, value in [("user.name", "Meshvale source-version probe"),
                               ("user.email", "version-probe@example.invalid"),
                               ("commit.gpgsign", "false"), ("core.autocrlf", "false"),
                               ("core.hooksPath", str(path / ".git/empty-hooks"))]:
                run([git, "-C", str(path), "config", "--local", key, value])
            run([git, "-C", str(path), "add", "--all"])
            run([git, "-C", str(path), "commit", "-qm", "Original identity fixture"])
            return path

        def tag(path, value, annotated=False):
            command = [git, "-C", str(path), "tag"]
            if annotated:
                command += ["-a", "-m", "Original fixture tag"]
            run([*command, value])

        def info(version="1.2.3", name="meshvale-interchange"):
            return f"Metadata-Version: 2.4\nName: {name}\nVersion: {version}\n\n"

        def check(label, path, version=None, provenance=None, failure=None, definitions=()):
            save_partial(label)
            ordinal = len(results)
            script = temporary / f"probe-{ordinal}.cmake"
            observed = temporary / f"observed-{ordinal}.txt"
            script.write_text(
                'cmake_minimum_required(VERSION 3.24)\n'
                f'include([===[{(path / "cmake/SourceVersion.cmake").as_posix()}]===])\n'
                f'meshvale_interchange_resolve_source_version(actual provenance [===[{path.as_posix()}]===])\n'
                'include(CMakePackageConfigHelpers)\n'
                f'write_basic_package_version_file([===[{(temporary / (str(ordinal)+"-ConfigVersion.cmake")).as_posix()}]===] VERSION "${{actual}}" COMPATIBILITY ExactVersion ARCH_INDEPENDENT)\n'
                f'file(WRITE [===[{observed.as_posix()}]===] "${{actual}}\\n${{provenance}}\\n")\n',
                encoding="utf-8")
            command = [cmake, "-DGIT_EXECUTABLE=" + git, *["-D" + value for value in definitions], "-P", str(script)]
            process = subprocess.run(command, env=environment, capture_output=True)
            record_command(command, process)
            save_partial(label)
            output = (process.stdout + process.stderr).decode("utf-8", errors="replace")
            if failure is not None:
                assert process.returncode != 0 and failure in " ".join(output.split()), (label, process.returncode, output)
                assert not observed.exists(), label
                row = {"case": label, "expected_configuration_error": failure, "actual_exit": process.returncode}
            else:
                assert process.returncode == 0, (label, output)
                values = observed.read_text().splitlines()
                assert values == [version, provenance], (label, values, version, provenance)
                row = {"case": label, "version": values[0], "provenance": values[1], "actual_exit": 0}
            results.append(row)
            save_partial()
            print(json.dumps(row), flush=True)

        raw = source("raw copy", marker=None)
        check("raw-copy", raw, "0.0.0", "unversioned-development")
        neutral = source("neutral template")
        check("all-unexpanded-template", neutral, "0.0.0", "unversioned-development")
        packaged = source("metadata path \u4e2d\u6587 \U0001f600", info())
        check("product-named-stable-sdist", packaged, "1.2.3", "sdist-release")
        check("missing-git-sdist", packaged, "1.2.3", "sdist-release", definitions=["CMAKE_DISABLE_FIND_PACKAGE_Git=TRUE"])
        for value, expected in [("0.0.0", "0.0.0"), ("0.0.1.dev134+g48298b24b", "0.0.0"),
                                ("0.0.1.DEV1+g123.d20261025", "0.0.0"), ("1.2.3+local_1-ab.c", "0.0.0"),
                                ("01.002.0003", "1.2.3")]:
            target = source("metadata-" + str(len(results)), info(value))
            check("version-" + value, target, expected, "sdist-release" if expected != "0.0.0" else "sdist-development")
        for value in ["1.2.3rc1", "1.2.3.post1", "1!1.2.3", "1.2.3.4", "garbage", ""]:
            target = source("bad-version-" + str(len(results)), info(value))
            check("unsupported-or-malformed-" + value, target, failure="Interchange source version:")
        for label, text in [
            ("duplicate-name", info().replace("Version:", "Name: meshvale-interchange\nVersion:")),
            ("duplicate-version", info().replace("\n\n", "\nVersion: 1.2.3\n\n")),
            ("wrong-product", info(name="meshvale-geometry")),
            ("folded-identity", info().replace("Version: 1.2.3", "Version: 1.2.3\n continuation")),
            ("missing-version", "Name: meshvale-interchange\n\nVersion: 1.2.3\n"),
        ]:
            check(label, source(label, text), failure="Interchange source version:")
        normalized = source("normalized headers", "nAmE:\t MESHVALE__..INTERCHANGE \t\r\nvErSiOn: 1.2.3\r\nOther: x\r\n folded unrelated\r\n\r\nName: wrong\r\nVersion: 9.9.9\r\n")
        check("normalized-header-and-body-boundary", normalized, "1.2.3", "sdist-release")
        stable = repository(source("original Git"))
        tag(stable, "v1.2.3", annotated=True)
        check("clean-exact-tag", stable, "1.2.3", "git-release")
        tag(stable, "v01.02.003")
        tag(stable, "v8", annotated=True)
        check("equivalent-and-unrelated-tags", stable, "1.2.3", "git-release")
        tag(stable, "v2.0.0")
        check("conflicting-exact-tags", stable, failure="ambiguous exact stable tags")
        run([git, "-C", str(stable), "tag", "-d", "v2.0.0"])
        (stable / "README").write_text("tracked edit\n", encoding="utf-8")
        check("tracked-dirty", stable, "0.0.0", "git-development")
        (stable / "README").write_text("original fixture\n", encoding="utf-8")
        (stable / "untracked").write_text("not ignored", encoding="utf-8")
        check("untracked-dirty", stable, "0.0.0", "git-development")
        check("untracked-stable-full-conflict", stable, failure="backend FULL is not release provenance", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=1.2.3"])
        (stable / "untracked").unlink()
        (stable / ".ignored").mkdir(); (stable / ".ignored/noise").write_text("ignored", encoding="utf-8")
        check("ignored-generated-data", stable, "1.2.3", "git-release")
        check("backend-full-short-disagreement", stable, "1.2.3", "git-release", definitions=["SKBUILD_PROJECT_NAME=MESHVALE_interchange", "SKBUILD_PROJECT_VERSION_FULL=1.2.3", "SKBUILD_PROJECT_VERSION=99.0.0"])
        check("unrelated-inherited-backend", stable, "1.2.3", "git-release", definitions=["SKBUILD_PROJECT_NAME=unrelated-parent", "SKBUILD_PROJECT_VERSION_FULL=garbage"])
        check("matching-short-only-backend", stable, failure="requires SKBUILD_PROJECT_VERSION_FULL", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION=1.2.3"])
        check("backend-release-mismatch", stable, failure="backend FULL conflicts", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=2.3.4"])
        check("no-git-own-marker", stable, "0.0.0", "git-unverified-development", definitions=["CMAKE_DISABLE_FIND_PACKAGE_Git=TRUE"])
        check("dev-full", raw, "0.0.0", "unversioned-development", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=0.0.1.dev2+g123.d20261025"])
        check("fallback-full", raw, "0.0.0", "unversioned-development", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=0.0.0"])
        check("full-cannot-promote-copy", raw, failure="backend FULL is not release provenance", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=1.2.3"])
        stale = repository(source("stale Git metadata", "Name: wrong\nVersion: 99.0.0\n\n", marker="malformed\n"))
        tag(stale, "v1.2.3")
        check("own-git-ignores-stale-metadata", stale, "1.2.3", "git-release")
        worktree = temporary / "proper worktree"
        run([git, "-C", str(stable), "worktree", "add", "--detach", str(worktree), "HEAD"])
        check("proper-worktree", worktree, "1.2.3", "git-release")

        def archive(label, repository_path, revision):
            path = temporary / (label + ".tar")
            run([git, "-C", str(repository_path), "archive", "--format=tar", "--output", str(path), revision])
            target = temporary / label; target.mkdir()
            with tarfile.open(path) as packaged:
                for member in packaged.getmembers():
                    name = PurePosixPath(member.name)
                    assert not name.is_absolute() and ".." not in name.parts and (member.isfile() or member.isdir())
                for member in packaged.getmembers():
                    destination = target.joinpath(*PurePosixPath(member.name).parts)
                    if member.isdir():
                        destination.mkdir(parents=True, exist_ok=True)
                    else:
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        destination.write_bytes(packaged.extractfile(member).read())
            return target

        # Remove the unrelated v8 tag before positive archive/backend-route cases.
        run([git, "-C", str(stable), "tag", "-d", "v8"])
        exact = archive("actual tagged archive", stable, "HEAD")
        check("actual-git-archive-stable", exact, "1.2.3", "archive-release")
        check("actual-git-archive-of-tree", archive("actual tree archive", stable, "HEAD^{tree}"), "0.0.0", "unversioned-development")
        (stable / "README").write_text("recognized ancestor development\n", encoding="utf-8")
        run([git, "-C", str(stable), "add", "README"]); run([git, "-C", str(stable), "commit", "-qm", "Advance original fixture"])
        check("own-git-ancestor-development", stable, "0.0.0", "git-development")
        development = archive("actual development archive", stable, "HEAD")
        check("actual-git-archive-development", development, "0.0.0", "archive-development")
        no_tags = repository(source("original no tags"))
        check("actual-no-tag-archive-native-only", archive("actual no tag archive", no_tags, "HEAD"), "0.0.0", "archive-development")
        marker_lines = (exact / ".git_archival.txt").read_text().splitlines()
        node = next(line.removeprefix("node: ") for line in marker_lines if line.startswith("node: "))
        for described_tag in ["v1.2.3", "v1.2.3rc1", "v8"]:
            inconsistent = "\n".join("describe-name: " + described_tag + "-1-g" + node[:8] if line.startswith("describe-name:") else line for line in marker_lines) + "\n"
            check("nonzero-history-with-exact-refs-" + described_tag, source("contradiction-" + str(len(results)), marker=inconsistent), failure="nonexact describe conflicts")
        for punctuation in ["v1.2.3,extra", "v1.2.3;extra", "v8-4-gdeadbeef", "v1.2.3$extra", "v1.2.3$Format"]:
            punctuated = repository(source("punctuation-" + str(len(results))))
            tag(punctuated, punctuation, annotated=True)
            check("whole-live-tag-" + punctuation, punctuated, "0.0.0", "git-development")
            check("whole-archive-tag-" + punctuation, archive("punctuation-archive-" + str(len(results)), punctuated, "HEAD"), "0.0.0", "archive-development")
            tag(punctuated, "v2.3.4")
            check("valid-live-tag-with-" + punctuation, punctuated, "2.3.4", "git-release")
            check("valid-archive-tag-with-" + punctuation, archive("punctuation-coexist-" + str(len(results)), punctuated, "HEAD"), "2.3.4", "archive-release")
        # Literal independent edits exercise contradictory archive channels.
        archived_marker = (exact / ".git_archival.txt").read_text()
        for label, marker in [
            ("partial-template", MARKER.replace("$Format:%H$", "a" * 40)),
            ("wrong-archive-product", MARKER.replace("meshvale-interchange", "meshvale-geometry")),
            ("duplicate-archive-field", MARKER + "node: " + "a" * 40 + "\n"),
            ("missing-archive-field", MARKER.replace("node-date: $Format:%cI$\n", "")),
            ("malformed-archive-node", archived_marker.replace("node: ", "node: invalid")),
            ("contradictory-exact-archive", archived_marker.replace("describe-name: v", "describe-name: v9")),
        ]:
            check(label, source(label, marker=marker), failure="Interchange source version:")
        (development / "PKG-INFO").write_text(info(), encoding="utf-8")
        check("development-archive-release-sdist-conflict", development, failure="development archive conflicts")
        (exact / "PKG-INFO").write_text(info("1.2.3+local"), encoding="utf-8")
        check("release-archive-local-sdist-conflict", exact, failure="conflicting archive/PKG-INFO")
        (exact / "PKG-INFO").write_text(info(), encoding="utf-8")
        check("release-archive-matching-sdist", exact, "1.2.3", "archive-release")
        zero = repository(source("positive zero")); tag(zero, "v0.0.0")
        check("positive-zero-tag", zero, "0.0.0", "git-release", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=0.0.0"])
        zero_archive = archive("positive zero archive", zero, "HEAD")
        check("positive-zero-archive-full", zero_archive, "0.0.0", "archive-release", definitions=["SKBUILD_PROJECT_NAME=meshvale-interchange", "SKBUILD_PROJECT_VERSION_FULL=0.0.0"])
        (zero_archive / "PKG-INFO").write_text(info("0.0.1.dev1"), encoding="utf-8")
        check("positive-zero-release-conflicts-dev-sdist", zero_archive, failure="conflicting archive/PKG-INFO")
        for control in ["\v", "\f"]:
            check("non-ASCII-header-trim-" + str(ord(control)), source("header-control-" + str(len(results)), info(control + "1.2.3")), failure="malformed version identity")
        parent = repository(source("outer original repository", marker=None))
        child = source("outer original repository/embedded source", info("2.3.4"))
        run([git, "-C", str(parent), "add", "--all"]); run([git, "-C", str(parent), "commit", "-qm", "Embed a separate source copy"]); tag(parent, "v9.9.9")
        check("unrelated-parent-git", child, "2.3.4", "sdist-release")

        # Real CMake directory scopes, without enabling or compiling any language.
        siblings = temporary / "sibling configuration"; siblings.mkdir()
        first = source("sibling configuration/first", info("1.2.3"))
        second = source("sibling configuration/second", info("2.3.4"))
        for path in [first, second]:
            (path / "CMakeLists.txt").write_text('include(cmake/SourceVersion.cmake)\nmeshvale_interchange_resolve_source_version(v p "${CMAKE_CURRENT_SOURCE_DIR}")\nproject(Identity VERSION "${v}" LANGUAGES NONE)\nfile(WRITE "${CMAKE_CURRENT_BINARY_DIR}/identity.txt" "${PROJECT_VERSION}\\n${p}\\n")\n', encoding="utf-8")
        (siblings / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.24)\nproject(Outer VERSION 9.8.7 LANGUAGES NONE)\nset(SKBUILD_PROJECT_NAME unrelated-parent)\nset(SKBUILD_PROJECT_VERSION_FULL 99.0.0)\nadd_subdirectory(first)\nadd_subdirectory(second)\nif(NOT PROJECT_VERSION STREQUAL "9.8.7")\n message(FATAL_ERROR "Child identity leaked into parent")\nendif()\n', encoding="utf-8")
        command = [cmake, "-DGIT_EXECUTABLE=" + git, "-S", str(siblings), "-B", str(temporary / "sibling-build")]
        if shutil.which("ninja"):
            command += ["-G", "Ninja"]
        run(command)
        for name, version in [("first", "1.2.3"), ("second", "2.3.4")]:
            assert (temporary / "sibling-build" / name / "identity.txt").read_text().splitlines() == [version, "sdist-release"]
        results.append({"case": "actual-add-subdirectory-project-scope", "versions": ["1.2.3", "2.3.4"], "parent_version": "9.8.7", "actual_exit": 0})
        assert temporary.is_relative_to(Path(tempfile.gettempdir()).resolve())

    result = {"completed": True, "scope": "CMake-only source identity/config-version and original Git/archive fixtures; no native compilation, backend build or release/runtime-floor evidence.",
              "case_count": len(results), "cases": results, "commands": commands,
              "module_sha256": hashlib.sha256((ROOT / "cmake/SourceVersion.cmake").read_bytes()).hexdigest(),
              "pinned_python_route_limits": ["No-tag archive may fail pinned SCM before CMake", "Interfering numeric tag may preempt SCM", "Untracked source plus stable FULL intentionally conflicts"]}
    if args.output:
        (args.output / "receipt.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
