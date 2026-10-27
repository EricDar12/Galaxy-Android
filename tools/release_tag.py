#!/usr/bin/env python3
"""Resolve and validate the tag a release run builds and publishes.

release.yml used to paste `${{ github.event.inputs.tag_name }}` straight into a
shell command line, where whoever dispatched the run can put anything, and that
same value selects both the source that gets checked out and the name the release
appears under.  The workflow reads it from the environment now and this script
matches it against a full pattern, so a newline, a quote or a semicolon cannot
hide inside a tag.

Usage: python tools/release_tag.py --output "$GITHUB_OUTPUT"
Exit status: 0 when a usable tag was written, 1 when the run is refused.
"""
import argparse
import os
import re
import sys

# A tag this project releases under: "v", then at least one tag-name character.
# The match is anchored at both ends, which is what rejects "v0.1; rm -rf /".
TAG_PATTERN = re.compile(r"v[A-Za-z0-9._-]+")


def validate(tag):
    """(tag, reason) - exactly one of the two is empty."""
    if not tag:
        return "", "REFUSING TO RELEASE: no tag name"
    if not TAG_PATTERN.fullmatch(tag):
        return "", ("REFUSING TO RELEASE: %r is not a tag name of 'v' followed by "
                    "letters, digits, '.', '_' or '-'" % tag)
    return tag, ""


def resolve(event_name, requested_tag, ref_name):
    """The tag this event describes: the dispatch input, or the pushed ref."""
    if event_name == "workflow_dispatch":
        return validate(requested_tag)
    return validate(ref_name)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event-name", default=os.environ.get("GITHUB_EVENT_NAME", ""))
    parser.add_argument("--requested-tag", default=os.environ.get("REQUESTED_TAG", ""))
    parser.add_argument("--ref-name", default=os.environ.get("GITHUB_REF_NAME", ""))
    parser.add_argument("--tag", help="check this tag instead of resolving one from the event")
    parser.add_argument("--output", help="file to append TAG=<tag> to, usually $GITHUB_OUTPUT")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    if args.tag is None:
        tag, problem = resolve(args.event_name, args.requested_tag, args.ref_name)
    else:
        tag, problem = validate(args.tag)
    if problem:
        print(f"[RELEASE] {problem}", file=sys.stderr)
        return 1
    line = f"TAG={tag}"
    if args.output:
        with open(args.output, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    else:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
