"""Release tag resolution: the dispatch input must never become shell text."""
import contextlib
from pathlib import Path
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

from tools.release_tag import TAG_PATTERN, main, resolve, validate

WORKFLOWS = Path(__file__).resolve().parents[2] / ".github" / "workflows"
RELEASE_WORKFLOW = WORKFLOWS / "release.yml"

ACCEPTED = ["v0.6.0-mobile", "v0.0.4a", "v1.2.3-beta.4", "v0.1", "v2_0.0-rc.1"]
REFUSED = [
    "", "v", "V0.6.0", "0.6.0", "refs/tags/v0.6.0", "../v0.6.0", "v0.6/0",
    "v0.6.0; rm -rf /", "v0.6.0\nmalicious", "v0.6.0\t", "v$(whoami)", "v`id`",
    "v'a", 'v"x"', "v|tail", "v&&ls", "v1\x00", "--output=pwn",
]
# Environment variables cannot reliably carry control characters on Windows, and
# the workflow step reads its tag from one, so its behavioural test uses the
# printable half.
SAFE_REFUSED = [tag for tag in REFUSED if "\n" not in tag and "\t" not in tag and "\x00" not in tag]

INPUT_CONTEXT = re.compile(r"github\.event\.inputs\.|\binputs\.")


def run_block_lines(text):
    """(line_number, line) for every line that the runner feeds to a shell."""
    lines = text.splitlines()
    blocks = []
    index = 0
    while index < len(lines):
        match = re.match(r"^(\s*)(?:-\s+)?run:\s*\|?", lines[index])
        if not match:
            index += 1
            continue
        key_indent = len(match.group(1)) + (4 if lines[index].lstrip().startswith("- ") else 0)
        cursor = index + 1
        while cursor < len(lines):
            line = lines[cursor]
            if line.strip() and len(line) - len(line.lstrip()) <= key_indent:
                break
            blocks.append((cursor + 1, line))
            cursor += 1
        index = cursor
    return blocks


class TestReleaseTag(unittest.TestCase):
    def test_released_tag_names_are_accepted(self):
        for tag in ACCEPTED:
            self.assertEqual(validate(tag), (tag, ""), tag)

    def test_shell_text_and_odd_strings_are_refused(self):
        for tag in REFUSED:
            accepted, problem = validate(tag)
            self.assertEqual(accepted, "", repr(tag))
            self.assertIn("REFUSING TO RELEASE", problem, repr(tag))

    def test_dispatch_input_and_pushed_ref_are_not_mixed(self):
        self.assertEqual(resolve("workflow_dispatch", "v1.2.3", "v9.9.9"), ("v1.2.3", ""))
        self.assertEqual(resolve("push", "", "v1.2.3"), ("v1.2.3", ""))
        self.assertEqual(resolve("push", "v1.2.3", "v1.2.3"), ("v1.2.3", ""))

    def test_refused_tag_never_reaches_the_output_file(self):
        # Whatever lands in $GITHUB_OUTPUT is interpolated into later steps, so a
        # refusal has to write nothing at all.
        for tag in REFUSED:
            with tempfile.TemporaryDirectory() as tmp:
                output = Path(tmp) / "github_output"
                output.write_text("existing=keep\n", encoding="utf-8")
                with contextlib.redirect_stderr(io.StringIO()) as err:
                    result = main(["--event-name=workflow_dispatch",
                                   f"--requested-tag={tag}", f"--output={output}"])
                self.assertEqual(result, 1, repr(tag))
                self.assertEqual(output.read_text(encoding="utf-8"), "existing=keep\n", repr(tag))
                self.assertIn("REFUSING TO RELEASE", err.getvalue(), repr(tag))

    def test_accepted_tag_is_written_for_the_later_steps(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "github_output"
            self.assertEqual(main(["--event-name=workflow_dispatch",
                                   "--requested-tag=v0.6.0-mobile",
                                   f"--output={output}"]), 0)
            self.assertEqual(output.read_text(encoding="utf-8"), "TAG=v0.6.0-mobile\n")

    def test_tag_handed_back_by_another_job_is_checked_again(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "github_output"
            self.assertEqual(main([f"--tag=v0.6.0-mobile", f"--output={output}"]), 0)
            output.unlink()
            for bad in ("", "v0.6.0; rm -rf /"):
                output.write_text("", encoding="utf-8")
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(main([f"--tag={bad}", f"--output={output}"]), 1, repr(bad))
                self.assertEqual(output.read_text(encoding="utf-8"), "", repr(bad))


def inline_guard_body(text, step_name):
    """The script of a workflow step, dedented, so it can be executed here."""
    lines = text.splitlines()
    start = next(index for index, line in enumerate(lines) if step_name in line)
    run_index = next(index for index in range(start, len(lines))
                     if lines[index].lstrip().startswith("run:"))
    following = next(index for index in range(run_index + 1, len(lines))
                     if re.match(r"^\s{0,8}(-\s+)?[A-Za-z_][A-Za-z0-9_-]*:", lines[index]))
    body = [(number, line) for number, line in run_block_lines(text)
            if run_index < number < following]
    if not body:
        raise AssertionError("step %r has no run block" % step_name)
    indent = min(len(line) - len(line.lstrip()) for _, line in body if line.strip())
    return "\n".join(line[indent:] for _, line in body)


class TestWorkflowInjection(unittest.TestCase):
    def test_dispatch_inputs_never_appear_in_a_run_block(self):
        offenders = []
        for workflow in sorted(WORKFLOWS.glob("*.yml")):
            text = workflow.read_text(encoding="utf-8")
            blocks = run_block_lines(text)
            # Guard against a scanner that silently finds nothing: the release
            # workflow alone scripts far more lines than this.
            self.assertGreater(len(blocks), 20, workflow.name)
            for number, line in blocks:
                if INPUT_CONTEXT.search(line):
                    offenders.append(f"{workflow.name}:{number}: {line.strip()}")
        self.assertEqual(offenders, [])

    def test_no_expression_at_all_reaches_a_shell_line(self):
        # github.event.inputs is the dangerous half of these, but an action output
        # such as a toolchain path is the same mechanism: whatever ${{ }} expands to
        # becomes script text.  Everything is routed through env:/with: instead, so a
        # value with a quote, a newline or a semicolon in it is data, not code.
        scanned = 0
        offenders = []
        for workflow in sorted(WORKFLOWS.glob("*.yml")):
            blocks = run_block_lines(workflow.read_text(encoding="utf-8"))
            scanned += len(blocks)
            for number, line in blocks:
                if "${{" in line:
                    offenders.append(f"{workflow.name}:{number}: {line.strip()}")
        self.assertGreater(scanned, 100, "the scanner found almost no script text to check")
        self.assertEqual(offenders, [])

    def test_the_detector_does_fire_on_a_pasted_expression(self):
        # Positive control for the test above: without this the assertion could pass
        # on a scanner that had stopped recognising run blocks.
        sample = (
            "jobs:\n"
            "  build:\n"
            "    steps:\n"
            "      - name: Unsafe\n"
            "        run: |\n"
            '          NDK_DIR="${{ steps.setup-ndk.outputs.ndk-path }}"\n'
        )
        blocks = run_block_lines(sample)
        self.assertEqual(len(blocks), 1)
        self.assertIn("${{", blocks[0][1])

    def test_run_block_scanner_finds_a_pasted_input_and_ignores_an_env_one(self):
        sample = (
            "jobs:\n"
            "  build:\n"
            "    steps:\n"
            "      - name: Safe\n"
            "        env:\n"
            "          TAG: ${{ inputs.tag_name }}\n"
            "        run: |\n"
            "          echo \"$TAG\"\n"
            "          python tools/release_tag.py\n"
            "      - name: Unsafe\n"
            "        run: |\n"
            "          echo \"${{ github.event.inputs.tag_name }}\"\n"
        )
        lines = run_block_lines(sample)
        self.assertEqual([number for number, _ in lines], [8, 9, 12])
        offenders = [line for _, line in lines if INPUT_CONTEXT.search(line)]
        self.assertEqual(len(offenders), 1)
        self.assertIn("github.event.inputs", offenders[0])

    def test_inline_guard_carries_the_same_pattern_as_the_script(self):
        # This step runs before the checkout, so it cannot call the script; the
        # two copies of the rule have to stay equal or one of them can drift.
        body = inline_guard_body(RELEASE_WORKFLOW.read_text(encoding="utf-8"),
                                 "Resolve release tag")
        self.assertIn("r'" + TAG_PATTERN.pattern + "'", body)

    def test_inline_guard_refuses_the_same_tags_as_the_script(self):
        body = inline_guard_body(RELEASE_WORKFLOW.read_text(encoding="utf-8"),
                                 "Resolve release tag")
        cases = [(tag, 0) for tag in ACCEPTED] + [(tag, 1) for tag in SAFE_REFUSED]
        for tag, expected in cases:
            with tempfile.TemporaryDirectory() as tmp:
                output = Path(tmp) / "github_output"
                output.write_text("", encoding="utf-8")
                env = dict(os.environ,
                           GITHUB_EVENT_NAME="workflow_dispatch",
                           REQUESTED_TAG=tag,
                           GITHUB_REF_NAME="v0.0.0-placeholder",
                           GITHUB_OUTPUT=str(output))
                result = subprocess.run([sys.executable, "-c", body], env=env,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, repr(tag))
                self.assertEqual(output.read_text(encoding="utf-8"),
                                 f"TAG={tag}\n" if expected == 0 else "", repr(tag))


if __name__ == "__main__":
    unittest.main()
