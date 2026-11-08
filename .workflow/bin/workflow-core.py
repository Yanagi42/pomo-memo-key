#!/usr/bin/env python3
"""workflow-core.py - `validate`, `next`, and `task` for the workflow toolkit.

Called by bin/workflow as `python3 workflow-core.py validate|next|task ...`.
Python 3.8+, standard library only. Reads task files under .workflow/tasks/
and .workflow/done/, specs under .workflow/specs/, and .workflow/decisions.md.

Metadata grammar (CONVENTIONS §2): fields are `Name: value` lines, plain,
bulleted (`- Name: value`) or bolded (`**Name:** value`); fenced code blocks
and HTML comments are ignored; indented or bulleted lines continue the
previous field (`Blocked by` joins with ", ", other fields with a space);
blank lines, headings, checklists and new fields end a continuation.
`## Check` starts a section whose lines join the same way. The first
occurrence of a field wins; later ones are duplicates.
"""

import json
import os
import re
import stat
import subprocess
import sys
import tempfile

VERSION = "2.10.0"
KNOWN_FIELDS = {
    "task", "effort", "task type", "base commit", "base branch", "delivers", "blocked by",
    "files", "read first", "check", "covers", "question", "time box", "priority",
    "status", "integration branch",
}
TASK_TYPES = ("feature", "bugfix", "spike", "refactor")
PRIORITY_RANK = {"critical": 0, "high": 1, "normal": 2, "low": 3}
SPEC_STATUSES = ("proposed", "in progress", "done")
TASK_STATUSES = ("active", "paused", "blocked")
FINISHED_STATUSES = ("done", "completed", "archived")
DECISION_REQUIRED_FIELDS = ("Decided", "Instead of", "Because")
DECISION_FIELDS = ("Decided", "Instead of", "Because", "Decided by", "Revisit when", "Supersedes")
SLUG_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
TASK_FILE_RE = re.compile(r"^([0-9]+)-.+\.md$")
FENCE_OPEN_RE = re.compile(r"^\s*(`{3,}|~{3,})(.*)$")
FENCE_CLOSE_RE = re.compile(r"^\s*(`{3,}|~{3,})\s*$")
TITLE_RE = re.compile(r"^#\s+([0-9]+):\s*(.*)$")
H2_RE = re.compile(r"^##\s+(.*)$")
CHECKLIST_RE = re.compile(r"^[-*]\s+\[[\sxX]\]")
BULLET_RE = re.compile(r"^[-*]\s+(.*)$")
BOLD_FIELD_RE = re.compile(r"^\*\*([^*]+)\*\*\s*:?\s*(.*)$")
PLAIN_FIELD_RE = re.compile(r"^([A-Za-z][A-Za-z0-9_. -]*):\s*(.*)$")
NONE_RE = re.compile(r"^none(\s*,\s*can\s+start\s+now)?$")
QUALIFIED_REF_RE = re.compile(r"^([a-z0-9]+(-[a-z0-9]+)*)/([0-9]+)$")
BARE_REF_RE = re.compile(r"^([0-9]+)$")
SPEC_CHECK_RE = re.compile(r"^[-*]\s+\*\*([A-Za-z][A-Za-z0-9_-]*[0-9])\.\*\*")
UNTICKED_RE = re.compile(r"^[-*]\s+\[ \]\s*(.*)$")
ACCEPTED_RE = re.compile(r"^\s*(?:[-*]\s+)?(?:\*\*Accepted:\*\*|Accepted:|\*\*Accepted\*\*:)[ \t]+\S")
REOPENED_RE = re.compile(r"^\s*##\s+Reopened(?:\b.*)?$", re.IGNORECASE)
STANDARDS_PLACEHOLDER_RE = re.compile(
    r"<TODO:[^>]+>|<(install|run|test|typecheck|lint) command>|<direct \| local-merge \| pr>"
)


# ---------------------------------------------------------------- helpers

def project_root():
    try:
        out = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             universal_newlines=True, check=False)
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except OSError:
        pass
    return os.getcwd()


def read_lines(path):
    """Lines of a file with the newline and one trailing CR removed."""
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fh:
        text = fh.read()
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return [line[:-1] if line.endswith("\r") else line for line in lines]


def norm_number(value):
    stripped = value.lstrip("0")
    return stripped if stripped else "0"


def task_ref(effort, number):
    return "%s/%02d" % (effort, int(number))


def is_safe_path(path, root):
    """True if path resolves within root."""
    try:
        real_root = os.path.realpath(root)
        real_path = os.path.realpath(path)
        common = os.path.commonpath([real_root, real_path])
        return common == real_root
    except (ValueError, OSError):
        return False


def is_subpath(path, base):
    """True if canonical path resolves strictly within canonical base directory."""
    try:
        real_base = os.path.realpath(base)
        real_path = os.path.realpath(path)
        if real_base == real_path:
            return False
        common = os.path.commonpath([real_base, real_path])
        return common == real_base
    except (ValueError, OSError):
        return False


def is_lexical_subpath(path, base):
    """True if normalized path is strictly within normalized base directory without resolving symlinks."""
    try:
        norm_base = os.path.abspath(base)
        norm_path = os.path.abspath(path)
        if norm_base == norm_path:
            return False
        common = os.path.commonpath([norm_base, norm_path])
        return common == norm_base
    except (ValueError, OSError):
        return False


def task_files(directory, root=None):
    """Every Markdown file under a task directory, at any depth, sorted."""
    found = []
    if not os.path.isdir(directory):
        return found
    if root is None:
        root = project_root()
    for base, dirs, files in os.walk(directory, followlinks=False):
        for name in files:
            if name.endswith(".md"):
                full = os.path.join(base, name)
                if os.path.islink(full) and not is_safe_path(full, root):
                    continue
                found.append(full)
        for name in dirs:
            full = os.path.join(base, name)
            if os.path.islink(full) and name.endswith(".md") and os.path.isfile(full):
                if not is_safe_path(full, root):
                    continue
                found.append(full)
    return sorted(found, key=lambda p: p.encode("utf-8", "surrogateescape"))


def dir_effort(base, path):
    rel = os.path.relpath(path, base).replace(os.sep, "/")
    return rel.split("/", 1)[0] if "/" in rel else ""


def strip_comments(line, in_comment):
    """Remove HTML comment text; returns (line, in_comment)."""
    while True:
        if in_comment:
            if "-->" in line:
                line = line.split("-->", 1)[1]
                in_comment = False
            else:
                return "", True
        elif "<!--" in line:
            before, after = line.split("<!--", 1)
            if "-->" in after:
                line = before + after.split("-->", 1)[1]
            else:
                return before, True
        else:
            return line, False


def split_text(text):
    """Lines of git output, split like read_lines (no trailing newline, no CR)."""
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return [line[:-1] if line.endswith("\r") else line for line in lines]


def git_result(root, *args):
    """(ok, stdout) of a git command run in root."""
    try:
        proc = subprocess.run(["git", "-C", root] + list(args), stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, universal_newlines=True)
    except OSError:
        return False, ""
    return proc.returncode == 0, proc.stdout


def next_fence_state(fence, line):
    """Updated fence `(char, length)` or `None`, plus True if `line` is a fence delimiter."""
    if fence is not None:
        m = FENCE_CLOSE_RE.match(line)
        if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= fence[1]:
            return None, True
        return fence, False
    m = FENCE_OPEN_RE.match(line)
    if m:
        delim, info = m.group(1), m.group(2)
        if delim[0] != "`" or "`" not in info:
            return (delim[0], len(delim)), True
    return None, False


def visible_lines(lines):
    """(lineno, line) outside fenced code blocks and HTML comments."""
    shown = []
    fence = None
    in_comment = False
    for lineno, line in enumerate(lines, 1):
        if fence is not None:
            fence, _ = next_fence_state(fence, line)
            continue
        was_comment = in_comment
        line, in_comment = strip_comments(line, in_comment)
        if (was_comment or in_comment) and not line.strip():
            continue
        fence, is_delim = next_fence_state(None, line)
        if is_delim:
            continue
        shown.append((lineno, line))
    return shown

def section_has_content(shown, heading):
    """True when a `## heading` section has at least one non-heading text line."""
    inside = False
    for _, line in shown:
        text = line.strip()
        m = H2_RE.match(text)
        if m:
            inside = m.group(1).strip().rstrip(":").strip().lower() == heading
            continue
        if text.startswith("#"):
            continue
        if inside and text:
            return True
    return False

def check_after_in_progress(shown):
    """True if shown lines contain a valid Check after field under ## Progress."""
    in_progress = False
    in_field = False
    for _, line in shown:
        raw = line
        text = line.strip()
        m2 = H2_RE.match(text)
        if m2:
            heading = m2.group(1).strip().rstrip(":").strip().lower()
            in_progress = (heading == "progress")
            in_field = False
            continue
        if not in_progress:
            continue
        if text.startswith("#"):
            in_field = False
            continue
        if not text:
            in_field = False
            continue
        if CHECKLIST_RE.match(text):
            in_field = False
            continue

        clean = text
        bm = BULLET_RE.match(clean)
        if bm:
            clean = bm.group(1).strip()
        fm = BOLD_FIELD_RE.match(clean) or PLAIN_FIELD_RE.match(clean)
        if fm:
            key = fm.group(1).lower().rstrip(":").strip()
            val = fm.group(2).strip()
            if key == "check after":
                in_field = True
                if val:
                    return True
                continue
            else:
                in_field = False
                continue
        if in_field and (raw[:1].isspace() or BULLET_RE.match(text)):
            if text:
                return True
    return False


class Meta(object):
    """Fields of one task or spec file."""

    def __init__(self):
        self.field = {}
        self.line = {}
        self.dups = []          # (line, name)
        self.title = ""
        self.title_number = ""

    def get(self, name, default=""):
        return self.field.get(name, default)


def read_fields(path, lines=None):
    meta = Meta()
    if lines is None:
        lines = read_lines(path)
    fence = None
    in_comment = False
    current = kind = None
    section_content = False
    title_seen = False
    sections = {}
    for lineno, line in enumerate(lines, 1):
        if fence is not None:
            fence, _ = next_fence_state(fence, line)
            continue
        line, in_comment = strip_comments(line, in_comment)
        fence, is_delim = next_fence_state(None, line)
        if is_delim:
            current = kind = None
            continue
        rawline = line
        line = line.strip()
        if not line:
            if kind and (kind == "field" or section_content):
                current = kind = None
            continue
        m = TITLE_RE.match(line)
        if m:
            if not title_seen:
                title_seen = True
                meta.title = m.group(2).strip()
                meta.title_number = norm_number(m.group(1))
            current = kind = None
            continue
        m = H2_RE.match(line)
        if m:
            heading = m.group(1).lower()
            if heading.endswith(":"):
                heading = heading[:-1]
            heading = heading.strip()
            current = kind = None
            if heading == "check":
                current, kind, section_content = heading, "section", False
            continue
        if line.startswith("#") or CHECKLIST_RE.match(line):
            current = kind = None
            continue
        clean = line
        m = BULLET_RE.match(clean)
        if m:
            clean = m.group(1)
        m = BOLD_FIELD_RE.match(clean) or PLAIN_FIELD_RE.match(clean)
        if m:
            key = m.group(1).lower()
            if key.endswith(":"):
                key = key[:-1]
            key = key.strip()
            if key in KNOWN_FIELDS:
                if key not in meta.line:
                    meta.field[key] = m.group(2).strip()
                    meta.line[key] = lineno
                    current, kind = key, "field"
                else:
                    meta.dups.append((lineno, key))
                    current, kind = None, "dup"
                continue
            current = kind = None
            continue
        if kind == "section":
            if current not in meta.line:
                old = meta.field.get(current, "")
                meta.field[current] = (old + " " + line) if old else line
            else:
                old = sections.get(current, "")
                sections[current] = (old + " " + line) if old else line
            section_content = True
            continue
        if kind in ("field", "dup") and (rawline[:1].isspace() or BULLET_RE.match(line)):
            if kind == "field":
                old = meta.field.get(current, "")
                sep = ", " if current == "blocked by" else " "
                meta.field[current] = (old + sep + clean) if old else clean
            continue
        current = kind = None
    if not meta.field.get("check") and sections.get("check"):
        meta.field["check"] = sections["check"]
    return meta


class BlockerError(Exception):
    def __init__(self, token, trailing=False):
        Exception.__init__(self, token)
        self.token = token
        self.trailing = trailing


def parse_blockers(raw):
    """[(normalized ref, raw token)] for a Blocked by value; raises BlockerError."""
    raw = raw.strip()
    if NONE_RE.match(raw.lower()):
        return []
    if not raw:
        raise BlockerError("")
    if raw.endswith(","):
        raise BlockerError("", trailing=True)
    refs = []
    for token in raw.split(","):
        token = token.strip()
        m = QUALIFIED_REF_RE.match(token)
        if m:
            refs.append((m.group(1) + "/" + norm_number(m.group(3)), token))
            continue
        m = BARE_REF_RE.match(token)
        if m:
            refs.append((norm_number(m.group(1)), token))
            continue
        raise BlockerError(token)
    return refs


def resolve_ref(ref, effort):
    if "/" in ref:
        ref_effort, number = ref.split("/", 1)
        return task_ref(ref_effort, number)
    return task_ref(effort, ref)


def colors_enabled(no_color_flag):
    return (not no_color_flag and not os.environ.get("NO_COLOR")
            and sys.stdout.isatty())


def usage_error(message, usage):
    sys.stderr.write("Error: %s\n" % message)
    if usage:
        sys.stderr.write(usage)
    return 2


# Options dropped in 2.5, answered like the removed commands in bin/workflow.
REMOVED_FLAGS = {
    "--fix": "validate --fix was removed in 2.5: CRLF line endings are read like LF, so there is nothing to repair",
    "--interactive": "next --interactive was removed in 2.5: run 'workflow next' and start a task with flow-implement <effort>/<NN>",
    "--quiet-idle": "pulse was replaced by 'workflow next --brief', which is silent when idle",
}


def parse_common(args, usage, allowed):
    """Parse shared CLI flags; returns (options, exit code or None)."""
    opts = {"format": "text", "no_color": False, "strict": False,
            "audit": False, "staged": False,
            "all": False, "brief": False, "effort": None}
    i = 0
    while i < len(args):
        arg = args[i]
        if arg in ("-h", "--help"):
            sys.stdout.write(usage)
            return opts, 0
        if arg == "--no-color":
            opts["no_color"] = True
        elif arg == "--strict" and "strict" in allowed:
            opts["strict"] = True
        elif arg == "--audit" and "audit" in allowed:
            opts["audit"] = True
        elif arg == "--staged" and "staged" in allowed:
            opts["staged"] = True
        elif arg == "--all" and "all" in allowed:
            opts["all"] = True
        elif arg == "--brief" and "brief" in allowed:
            opts["brief"] = True
        elif arg == "--effort" and "effort" in allowed:
            if i + 1 >= len(args) or not args[i + 1]:
                return opts, usage_error("--effort requires an effort slug", "")
            opts["effort"] = args[i + 1]
            i += 1
        elif arg.startswith("--effort=") and "effort" in allowed:
            opts["effort"] = arg[len("--effort="):]
            if not opts["effort"]:
                return opts, usage_error("--effort requires an effort slug", "")
        elif arg == "--format":
            if i + 1 >= len(args):
                return opts, usage_error("--format requires text or json", "")
            opts["format"] = args[i + 1]
            i += 1
        elif arg.startswith("--format="):
            opts["format"] = arg[len("--format="):]
        elif arg in REMOVED_FLAGS:
            return opts, usage_error(REMOVED_FLAGS[arg], "")
        else:
            return opts, usage_error("unknown option: %s" % arg, usage)
        i += 1
    if opts["effort"] is not None and not SLUG_RE.match(opts["effort"]):
        return opts, usage_error("invalid effort slug: %s" % opts["effort"], "")
    if opts["format"] not in ("text", "json"):
        return opts, usage_error("invalid format: %s (use text or json)" % opts["format"], "")
    if opts["brief"] and opts["format"] == "json":
        return opts, usage_error("--brief cannot be combined with --format json", "")
    return opts, None


# --------------------------------------------------------------- validate

VALIDATE_USAGE = """workflow validate - validate task files and the decision log

Usage: workflow validate [--strict] [--audit] [--staged] [--format text|json] [--no-color]
Checks required Task, Effort, Check and Blocked by fields; effort slugs;
that a task in tasks/<effort>/ says the same Effort; that the heading number
matches the filename; Status and Priority values; blockers; Base commit refs;
task-number collisions within efforts; dependency cycles; Covers IDs no spec
check defines; and decision format. Checks active and completed task files;
problems in completed files warn instead of failing.

  --audit        Audit historical archives and spec acceptances (omitted by default)
  --staged       Validate staged git index content instead of the working tree
  --strict       Treat warnings as errors
  --format TYPE  Output text (default) or JSON
  --no-color     Disable terminal colors
  -h, --help     Show this help
Exit: 0 = valid, 1 = warnings, 2 = errors (including usage errors).
"""


class Validator(object):
    def __init__(self, root, audit=False, staged=False):
        self.root = root
        self.audit = audit
        self.staged = staged
        self.wf = os.path.join(root, ".workflow")
        self.issues = []
        self.errors = self.warnings = 0
        self.files = []
        self.meta = {}
        self.number = {}
        self.effort = {}
        self.dir_effort = {}
        self.index = {}
        self.active = set()
        self.tasks_dir = os.path.join(self.wf, "tasks") + os.sep
        self.done_dir = os.path.join(self.wf, "done") + os.sep
        self.lines_cache = {}

    def file_lines(self, path):
        if self.staged:
            rel = self.rel(path)
            ready, text = git_result(self.root, "show", ":%s" % rel)
            if not ready:
                return None
            return split_text(text)
        if not os.path.isfile(path):
            return None
        if os.path.islink(path) and not is_safe_path(path, self.root):
            return None
        return read_lines(path)
    def rel(self, path):
        rel = os.path.relpath(path, self.root)
        return rel.replace(os.sep, "/")

    def issue(self, severity, path, line, message, suggestion):
        if severity == "ERROR":
            self.errors += 1
        else:
            self.warnings += 1
        self.issues.append({"severity": severity, "file": self.rel(path),
                            "line": int(line or 1), "message": message,
                            "suggestion": suggestion})

    def sev(self, path):
        return "WARNING" if path.startswith(self.done_dir) else "ERROR"

    def line_of(self, path, key):
        return self.meta[path].line.get(key, 1)

    def run(self):
        if self.staged:
            ready, out = git_result(self.root, "ls-files", "--cached", "--", ".workflow")
            if not ready or not out.strip():
                if not os.path.isdir(self.wf):
                    self.issue("ERROR", self.wf, 1, "missing .workflow directory", "Run: workflow init")
                    return
            ready, out = git_result(self.root, "ls-files", "--cached", "--", ".workflow/tasks")
            if not ready or not out.strip():
                if not os.path.isdir(os.path.join(self.wf, "tasks")):
                    self.issue("WARNING", os.path.join(self.wf, "tasks"), 1,
                               "tasks directory is missing", "Run: workflow init")
        else:
            if not os.path.isdir(self.wf):
                self.issue("ERROR", self.wf, 1, "missing .workflow directory", "Run: workflow init")
                return
            if not os.path.isdir(os.path.join(self.wf, "tasks")):
                self.issue("WARNING", os.path.join(self.wf, "tasks"), 1,
                           "tasks directory is missing", "Run: workflow init")
        self.collect()
        self.check_required()
        self.check_types_and_slugs()
        self.check_layout()
        self.check_collisions()
        edges = self.check_blockers()
        self.check_status()
        self.check_priorities()
        self.check_covers()
        self.check_bases()
        self.check_files_field()
        self.check_cycles(edges)
        self.check_new_archives()
        self.check_spec_done_with_open_tasks()
        self.check_spec_acceptance()
        self.check_decisions()
        self.check_standards()

    def collect(self):
        if self.staged:
            ready, listing = git_result(self.root, "ls-files", "--stage", "--",
                                        ".workflow/tasks", ".workflow/done")
            if ready and listing:
                for line in listing.splitlines():
                    parts = line.split(None, 2)
                    if len(parts) < 3:
                        continue
                    mode = parts[0]
                    rest = parts[2].split("\t", 1)
                    if len(rest) != 2:
                        continue
                    stage_num, rel = rest[0].strip(), rest[1].strip()
                    if stage_num != "0" or not rel.endswith(".md"):
                        continue
                    if mode == "120000":
                        s_ready, s_target = git_result(self.root, "cat-file", "-p", parts[1])
                        if s_ready:
                            target = s_target.strip()
                            resolved = os.path.normpath(os.path.join(os.path.dirname(os.path.join(self.root, rel)), target))
                            if not is_safe_path(resolved, self.root):
                                continue
                    basename = os.path.basename(rel)
                    m = TASK_FILE_RE.match(basename)
                    if not m:
                        continue
                    path = os.path.join(self.root, rel)
                    self.files.append(path)
                    self.number[path] = norm_number(m.group(1))
                    rel_parts = rel.split("/")
                    sub = rel_parts[1] if len(rel_parts) >= 2 else "tasks"
                    base_dir = os.path.join(self.wf, sub)
                    self.dir_effort[path] = dir_effort(base_dir, path)
                    meta = read_fields(path, lines=self.lines(path))
                    self.meta[path] = meta
                    for lno, name in meta.dups:
                        self.issue(self.sev(path), path, lno, "duplicate %s field" % name,
                                   "Keep one %s field." % name)
                    self.effort[path] = meta.get("effort")
                    key = task_ref(self.effort[path], self.number[path])
                    self.index.setdefault(key, path)
                    if sub == "tasks":
                        self.active.add(key)
            self.files.sort(key=lambda p: p.encode("utf-8", "surrogateescape"))
            return

        for sub in ("tasks", "done"):
            directory = os.path.join(self.wf, sub)
            for path in task_files(directory, root=self.root):
                m = TASK_FILE_RE.match(os.path.basename(path))
                if not m:
                    continue
                self.files.append(path)
                self.number[path] = norm_number(m.group(1))
                self.dir_effort[path] = dir_effort(directory, path)
                meta = read_fields(path, lines=self.lines(path))
                self.meta[path] = meta
                for line, name in meta.dups:
                    self.issue(self.sev(path), path, line, "duplicate %s field" % name,
                               "Keep one %s field." % name)
                self.effort[path] = meta.get("effort")
                key = task_ref(self.effort[path], self.number[path])
                self.index.setdefault(key, path)
                if sub == "tasks":
                    self.active.add(key)
    def check_required(self):
        for path in self.files:
            meta = self.meta[path]
            for name in ("Task", "Effort", "Check", "Blocked by"):
                if name == "Task":
                    value = meta.title or meta.get("task")
                    line = 1 if meta.title else meta.line.get("task", 1)
                else:
                    key = name.lower()
                    value = meta.get(key)
                    line = meta.line.get(key, 1)
                if not value:
                    self.issue(self.sev(path), path, line,
                               "missing or empty required %s field" % name,
                               "Add a nonempty %s field (Task may be '# NN: title'; "
                               "Check may be a section)." % name)

    def check_types_and_slugs(self):
        for path in self.files:
            value = self.meta[path].get("task type")
            if value and value not in TASK_TYPES:
                self.issue(self.sev(path), path, self.line_of(path, "task type"),
                           "invalid Task type '%s'" % value,
                           "Use feature, bugfix, spike, or refactor.")
            effort = self.effort[path]
            if effort and not SLUG_RE.match(effort):
                self.issue(self.sev(path), path, self.line_of(path, "effort"),
                           "invalid effort slug '%s'" % effort,
                           "Use lowercase letters, digits and hyphens.")

    def check_layout(self):
        for path in self.files:
            directory, effort = self.dir_effort[path], self.effort[path]
            if directory and effort and directory != effort:
                self.issue(self.sev(path), path, self.line_of(path, "effort"),
                           "task is under %s/ but its Effort is '%s'" % (directory, effort),
                           "Move it to the %s/ directory or correct the Effort field." % effort)
            title_number = self.meta[path].title_number
            if title_number and title_number != self.number[path]:
                self.issue(self.sev(path), path, 1,
                           "heading number %s does not match filename number %s"
                           % (title_number, self.number[path]),
                           "Make the '# NN:' heading and the filename use the same number.")

    def check_collisions(self):
        seen = {}
        for path in self.files:
            key = task_ref(self.effort[path], self.number[path])
            if key in seen:
                self.issue("ERROR", path, 1,
                           "task-number collision: %s also occurs in %s" % (key, self.rel(seen[key])),
                           "Renumber one task and update its blocker references.")
            else:
                seen[key] = path

    def check_blockers(self):
        edges = {}
        suggestion = "Use NN or effort/NN, comma-separated, or None."
        for path in self.files:
            blocked = self.meta[path].get("blocked by")
            if not blocked:
                continue
            line = self.line_of(path, "blocked by")
            key = task_ref(self.effort[path], self.number[path])
            try:
                refs = parse_blockers(blocked)
            except BlockerError as exc:
                if exc.trailing:
                    self.issue(self.sev(path), path, line, "malformed blocker: trailing comma", suggestion)
                else:
                    self.issue(self.sev(path), path, line, "malformed blocker '%s'" % exc.token, suggestion)
                continue
            for ref, raw in refs:
                target = resolve_ref(ref, self.effort[path])
                if target not in self.index:
                    self.issue(self.sev(path), path, line, "orphaned blocker '%s'" % raw,
                               "Create task %s or correct the Blocked by reference." % target)
                elif key in self.active and target in self.active:
                    edges.setdefault(key, []).append(target)
        return edges

    def check_covers(self):
        """Covers must name check IDs the effort's spec defines.

        Silent when the effort has no spec or the spec lists no IDs, so a project
        that writes its checks differently is never nagged.
        """
        cache = {}
        for path in self.files:
            covers = self.meta[path].get("covers")
            effort = self.effort[path]
            if not covers or not effort:
                continue
            if effort not in cache:
                cache[effort] = spec_check_ids(self.wf, effort)
            known = cache[effort]
            if not known:
                continue
            line = self.line_of(path, "covers")
            for token in covers.split(","):
                token = token.strip().strip(".")
                if token and token not in known:
                    self.issue(self.sev(path), path, line,
                               "Covers names '%s', which specs/%s.md does not define"
                               % (token, effort),
                               "Use one of: %s, or add the check to the spec."
                               % ", ".join(sorted(known)))

    def check_files_field(self):
        """Warn when an active non-spike task sets `Files:` to prose with no paths."""
        for path in self.files:
            if not path.startswith(self.tasks_dir):
                continue
            raw = self.meta[path].get("files", "").strip()
            if not raw or FILES_NONE_RE.match(raw) or re.fullmatch(r"<[^<>]+>", raw):
                continue
            if self.meta[path].get("task type", "").strip().lower() == "spike":
                continue
            if not parse_task_files_field(self.root, raw):
                self.issue("WARNING", path, self.line_of(path, "files"),
                           "Files field lists no recognizable paths",
                           "List repo-relative file or directory paths (comma- or space-separated), or omit Files.")


    def check_status(self):
        """A Status value decides whether `next` offers a task as ready.

        An unrecognized one is worse than a missing field: the task is silently
        treated as not started, so a half-finished task gets handed out again.
        """
        for path in self.files:
            value = self.meta[path].get("status").lower()
            if not value:
                continue
            line = self.line_of(path, "status")
            if value in TASK_STATUSES:
                continue
            if value in FINISHED_STATUSES:
                self.issue(self.sev(path), path, line,
                           "Status '%s' on a task file; only flow-implement archives a task"
                           % value,
                           "Move it to done/<effort>/ and drop the Status field.")
            else:
                self.issue(self.sev(path), path, line,
                           "invalid Status '%s'" % value,
                           "Use one of: %s (or drop the field for a drafted task)."
                           % ", ".join(TASK_STATUSES))

    def check_priorities(self):
        """The spec header's Priority is what `next` ranks by, and Status tracks lifecycle.

        A typo in Priority silently falls back to normal, so the effort loses its place
        in the queue without a word. A task-level Priority does nothing at all.
        """
        specs = {}
        for path in self.files:
            value = self.meta[path].get("priority")
            if value:
                self.issue(self.sev(path), path, self.line_of(path, "priority"),
                           "Priority on a task file, which nothing ranks by",
                           "Set Priority in specs/%s.md instead."
                           % (self.effort[path] or "<effort>"))
            effort = self.effort[path]
            if effort and effort not in specs:
                specs[effort] = True
        specs_dir = os.path.join(self.wf, "specs")
        if os.path.isdir(specs_dir):
            for name in sorted(os.listdir(specs_dir)):
                if name.endswith(".md"):
                    specs[name[:-3]] = True
        for effort in sorted(specs):
            path = os.path.join(self.wf, "specs", effort + ".md")
            if not os.path.isfile(path):
                continue
            meta = read_fields(path)
            value = meta.get("priority", "").lower()
            if value and value not in PRIORITY_RANK:
                self.issue("WARNING", path, meta.line.get("priority", 1),
                           "invalid Priority '%s'" % value,
                           "Use one of: %s. An unknown value ranks as normal."
                           % ", ".join(sorted(PRIORITY_RANK)))
            status = meta.get("status", "").lower()
            if status and status not in SPEC_STATUSES:
                self.issue("WARNING", path, meta.line.get("status", 1),
                           "invalid spec Status '%s'" % status,
                           "Use one of: %s." % ", ".join(SPEC_STATUSES))

    def check_bases(self):
        commits, branches = {}, {}
        for path in self.files:
            value = self.meta[path].get("base commit").strip()
            if value and value != "<commit-sha>":
                commits.setdefault(value, []).append(path)
            branch = self.meta[path].get("base branch").strip()
            if branch and not (branch.startswith("<") and branch.endswith(">")):
                branches.setdefault(branch, []).append(path)
        if not commits and not branches:
            return
        is_git = subprocess.run(["git", "-C", self.root, "rev-parse", "--git-dir"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        for value in sorted(commits):
            ok = False
            if is_git:
                ok = subprocess.run(["git", "-C", self.root, "rev-parse", "--verify", "--quiet",
                                     "--end-of-options", value + "^{commit}"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            if not ok:
                for path in commits[value]:
                    self.issue(self.sev(path), path, self.line_of(path, "base commit"),
                               "invalid Base commit '%s'" % value,
                               "Use an existing git commit ref, or <commit-sha> for drafts.")
        for branch in sorted(branches):
            ok = False
            if is_git:
                for candidate in (branch, "origin/" + branch):
                    if subprocess.run(["git", "-C", self.root, "rev-parse", "--verify", "--quiet",
                                       "--end-of-options", candidate + "^{commit}"],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                        ok = True
                        break
            if not ok:
                for path in branches[branch]:
                    self.issue(self.sev(path), path, self.line_of(path, "base branch"),
                               "invalid Base branch '%s'" % branch,
                               "Use an existing blocker branch name, or drop Base branch once rebased.")
    def check_cycles(self, edges):
        state = {}

        def visit(node, path):
            state[node] = 1
            for target in edges.get(node, ()):
                status = state.get(target, 0)
                if status == 1:
                    task = self.index[node]
                    self.issue("ERROR", task, self.line_of(task, "blocked by"),
                               "circular dependency: %s -> %s" % (" -> ".join(path), target),
                               "Remove or change one blocker in the cycle.")
                elif status == 0:
                    visit(target, path + [target])
            state[node] = 2

        sys.setrecursionlimit(max(1000, len(self.active) * 4 + 100))
        for node in sorted(self.active):
            if state.get(node, 0) == 0:
                visit(node, [node])

    def lines(self, path):
        if path not in self.lines_cache:
            res = self.file_lines(path)
            self.lines_cache[path] = res if res is not None else []
        return self.lines_cache[path]

    def head_exists(self):
        ready, _ = git_result(self.root, "rev-parse", "--verify", "--quiet", "HEAD^{commit}")
        return ready

    def check_new_archives(self):
        """Judge archives made in this change; committed history is never re-judged unless --audit is enabled.

        A task moves to done/ with its checkboxes ticked and a record of the Check
        run. Only paths absent from HEAD's tree are checked by default, so an old archive
        that predates the rule (or a checkout of one) stays quiet. Passing --audit checks
        all archived tasks.
        """
        committed = set()
        if not self.audit:
            if not self.head_exists():
                return
            ready, listing = git_result(self.root, "ls-tree", "-r", "--name-only", "HEAD", "--",
                                        ".workflow/done")
            if not ready:
                return
            committed = set(listing.splitlines())
        unticked_suggestion = ("Tick it if it is done; otherwise finish it, or delete it "
                               "and say in ## Progress why it no longer applies.")
        for path in self.files:
            rel = self.rel(path)
            if not rel.startswith(".workflow/done/"):
                continue
            if not self.audit and rel in committed:
                continue
            shown = visible_lines(self.lines(path))
            for lineno, line in shown:
                m = UNTICKED_RE.match(line.strip())
                if m:
                    item = m.group(1).strip()
                    if len(item) > 60:
                        item = item[:57] + "..."
                    self.issue("ERROR", path, lineno,
                               "archived with an unticked item: %s" % item, unticked_suggestion)
            if self.meta[path].get("task type").strip().lower() == "spike":
                if not section_has_content(shown, "findings"):
                    self.issue("ERROR", path, 1, "spike archived without ## Findings",
                               "Record what the spike found under ## Findings.")
            elif not check_after_in_progress(shown):
                self.issue("ERROR", path, 1, "archived without a Check after record",
                           "Run the Check on the final code and record `Check after:` in ## Progress.")
    def specs(self):
        """(effort, path, meta) for every spec file, sorted by effort."""
        found = []
        if self.staged:
            ready, listing = git_result(self.root, "ls-files", "--stage", "--", ".workflow/specs")
            if ready and listing:
                for line in listing.splitlines():
                    parts = line.split(None, 2)
                    if len(parts) < 3:
                        continue
                    rest = parts[2].split("\t", 1)
                    if len(rest) != 2:
                        continue
                    stage_num, rel = rest[0].strip(), rest[1].strip()
                    if stage_num != "0" or not rel.endswith(".md"):
                        continue
                    path = os.path.join(self.root, rel)
                    name = os.path.basename(rel)[:-3]
                    found.append((name, path, read_fields(path, lines=self.lines(path))))
            return sorted(found, key=lambda x: x[0])

        specs_dir = os.path.join(self.wf, "specs")
        if not os.path.isdir(specs_dir):
            return found
        for name in sorted(os.listdir(specs_dir)):
            path = os.path.join(specs_dir, name)
            if name.endswith(".md") and os.path.isfile(path):
                if os.path.islink(path) and not is_safe_path(path, self.root):
                    continue
                found.append((name[:-3], path, read_fields(path, lines=self.lines(path))))
        return found

    def check_spec_done_with_open_tasks(self):
        """A done spec whose effort still has a task under tasks/ was closed too early."""
        for effort, path, meta in self.specs():
            if meta.get("status").strip().lower() != "done":
                continue
            open_paths = sorted(p for p in self.files
                                if p.startswith(self.tasks_dir) and self.effort.get(p) == effort)
            if not open_paths:
                continue
            number = self.number[open_paths[0]]
            self.issue("ERROR", path, meta.line.get("status", 1),
                       "specs/%s.md says done but %s is still open"
                       % (effort, task_ref(effort, number)),
                       "Set the spec back to in progress (CONVENTIONS §4), or archive the task.")

    def check_spec_acceptance(self):
        """A spec that turns done in this change needs the user's acceptance on record.

        flow-close writes one `**Accepted:**` line per acceptance, and a reopening
        needs a fresh one, so the count must cover 1 + the number of ## Reopened
        headings. A spec already done in HEAD is history and stays quiet by default.
        Passing --audit checks all done specs.
        """
        for effort, path, meta in self.specs():
            if meta.get("status").strip().lower() != "done":
                continue
            if not self.audit:
                if not self.head_exists():
                    return
                ready, text = git_result(self.root, "show", "HEAD:.workflow/specs/%s.md" % effort)
                if ready and read_fields(path, split_text(text)).get("status").strip().lower() == "done":
                    continue
            required, accepted = 1, 0
            closeout = os.path.join(self.done_dir, effort, "closeout.md")
            lines = self.file_lines(closeout)
            if lines is not None:
                required += sum(1 for line in lines if REOPENED_RE.match(line))
                accepted = sum(1 for line in lines if ACCEPTED_RE.match(line))
            if accepted < required:
                self.issue("ERROR", path, meta.line.get("status", 1),
                           "specs/%s.md is marked done without a recorded acceptance" % effort,
                           "flow-close records `**Accepted:** <date> by <name>` in "
                           "done/%s/closeout.md once the user accepts." % effort)

    def check_decisions(self):
        path = os.path.join(self.wf, "decisions.md")
        lines = self.file_lines(path)
        if lines is None:
            self.issue("WARNING", path, 1, "decision log is missing",
                       "Create .workflow/decisions.md with ## Entries.")
            return
        for line, message, suggestion in decision_problems(lines):
            self.issue("ERROR", path, line, message, suggestion)

    def check_standards(self):
        path = os.path.join(self.wf, "standards.md")
        lines = self.file_lines(path)
        if lines is None:
            return
        for lineno, line in enumerate(lines, 1):
            if STANDARDS_PLACEHOLDER_RE.search(line):
                self.issue("WARNING", path, lineno,
                           "standards.md contains unfilled placeholder",
                           "Fill in or remove placeholders in .workflow/standards.md.")
                break


ISO_TIME_RE = re.compile(r"^[0-9-]+T[0-9][0-9]:[0-9][0-9](:[0-9][0-9](\.[0-9]+)?)?"
                         r"(Z|[+-][0-9][0-9]:[0-9][0-9])?$")
DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
DECISION_HEADING_RE = re.compile(r"^[^ \t]+[ \t]+-[ \t]+[^ \t]")
DECISION_OUTSIDE_RE = re.compile(r"^[ \t]*-[ \t]+\*\*(Decided|Instead of|Because|Decided by|Revisit when|Supersedes):")
DECIDED_BY_RE = re.compile(r"^(user|agent)([ \t.(]|$)")


def valid_iso(value):
    base = value.split("T", 1)[0]
    if not DATE_RE.match(base):
        return False
    year, month, day = int(base[:4]), int(base[5:7]), int(base[8:10])
    if month < 1 or month > 12:
        return False
    if month == 2:
        days = 29 if (year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)) else 28
    elif month in (4, 6, 9, 11):
        days = 30
    else:
        days = 31
    if day < 1 or day > days:
        return False
    if value == base:
        return True
    if not ISO_TIME_RE.match(value):
        return False
    rest = value[11:]
    hour, minute = int(rest[0:2]), int(rest[3:5])
    second = int(rest[6:8]) if rest[5:6] == ":" else 0
    if hour > 23 or minute > 59 or second > 59:
        return False
    m = re.search(r"[+-]", rest)
    if m:
        offset = rest[m.start() + 1:]
        if int(offset[0:2]) > 23 or int(offset[3:5]) > 59:
            return False
    return True


def decision_problems(lines):
    """[(line, message, suggestion)] for decisions.md content."""
    problems = []
    state = {"entry": False, "start": 0, "values": {}}

    def finish():
        if not state["entry"]:
            return
        for name in DECISION_REQUIRED_FIELDS:
            if not state["values"].get(name):
                problems.append((state["start"], "missing or empty decision field %s" % name,
                                 "Add - **%s:** meaningful content." % name))
        for opt_field in ("Revisit when", "Supersedes"):
            if opt_field in seen and not state["values"].get(opt_field):
                problems.append((state["start"], "missing or empty decision field %s" % opt_field,
                                 "Add - **%s:** meaningful content, or omit the field." % opt_field))
    fence = None
    in_comment = entries = False
    seen = {}
    pending = ""
    for number, s in enumerate(lines, 1):
        if fence is not None:
            fence, _ = next_fence_state(fence, s)
            continue
        if in_comment:
            if "-->" in s:
                in_comment = False
            continue
        if "<!--" in s:
            if "-->" not in s:
                in_comment = True
            continue
        fence, is_delim = next_fence_state(None, s)
        if is_delim:
            continue
        if re.match(r"^##[ \t]+Entries[ \t]*$", s):
            finish()
            entries, state["entry"] = True, False
            continue
        if not entries:
            continue
        if re.match(r"^##[ \t]+[^#]", s):
            finish()
            state.update(entry=True, start=number, values={})
            seen, pending = {}, ""
            title = re.sub(r"^##[ \t]+", "", s)
            date = re.split(r"[ \t]", title, maxsplit=1)[0]
            if not valid_iso(date) or not DECISION_HEADING_RE.match(title):
                problems.append((number, "malformed decision heading",
                                 "Use ## ISO-date - title with a valid ISO8601 date or timestamp."))
            continue
        if not state["entry"]:
            if DECISION_OUTSIDE_RE.match(s):
                problems.append((number, "decision field outside an entry",
                                 "Add ## ISO-date - title above it."))
            continue
        v = re.sub(r"^[ \t]*-[ \t]+", "", s.replace("**", ""), count=1)
        key = v.split(":", 1)[0]
        if key in DECISION_FIELDS:
            seen[key] = seen.get(key, 0) + 1
            if seen[key] > 1:
                problems.append((number, "duplicate decision field %s" % key,
                                 "Keep one %s field per entry." % key))
            v = v.split(":", 1)[1].strip(" \t\r") if ":" in v else ""
            state["values"][key] = bool(v)
            pending = key
            if key == "Decided by" and not DECIDED_BY_RE.match(v):
                problems.append((number, "invalid Decided by field",
                                 "Use user or agent."))
        elif pending and re.match(r"^[ \t]+[^ \t]", s):
            state["values"][pending] = True
        elif s.strip(" \t\r"):
            pending = ""
    finish()
    if not entries:
        problems.append((1, "missing ## Entries section", "Add ## Entries before the decision log."))
    return problems


def cmd_validate(args):
    opts, code = parse_common(args, VALIDATE_USAGE, ("strict", "audit", "staged"))
    if code is not None:
        return code
    root = project_root()
    if opts["staged"]:
        ready, _ = git_result(root, "rev-parse", "--is-inside-work-tree")
        if not ready:
            sys.stderr.write("Error: --staged requires a git repository\n")
            return 2
    validator = Validator(root, audit=opts["audit"], staged=opts["staged"])
    validator.run()
    errors, warnings = validator.errors, validator.warnings
    if errors or (opts["strict"] and warnings):
        status = 2
    elif warnings:
        status = 1
    else:
        status = 0
    if opts["format"] == "json":
        out = {"valid": status == 0 and errors == 0 and warnings == 0, "errors": errors,
               "warnings": warnings, "tasks": len(validator.files), "issues": validator.issues}
        sys.stdout.write(json.dumps(out, ensure_ascii=False, separators=(",", ":")) + "\n")
        return status
    color = colors_enabled(opts["no_color"])
    red, yellow, blue, off = ("\033[31m", "\033[33m", "\033[34m", "\033[0m") if color else ("",) * 4
    out = []
    for item in validator.issues:
        warn = item["severity"] == "WARNING"
        out.append("%s%s:%s %s:%d: %s\n  Fix: %s\n" % (
            yellow if warn else red, "Warning" if warn else "Error", off,
            item["file"], item["line"], item["message"], item["suggestion"]))
    if status == 0:
        out.append("%sWorkflow is valid (%d task files).%s\n" % (blue, len(validator.files), off))
    else:
        out.append("%d error(s), %d warning(s).\n" % (errors, warnings))
    sys.stdout.write("".join(out))
    return status


# ------------------------------------------------------------------- next

NEXT_USAGE = """workflow next - suggest what to work on next

Usage: workflow next [--effort SLUG] [--all] [--brief] [--format text|json] [--no-color]

Shows the top five ready tasks (or all with --all): not started, and every blocker archived under
.workflow/done/ ("Blocked by: None" also accepts "None, can start now"), with
their effort goal and Check preview. Efforts rank by the **Priority:** of
.workflow/specs/<effort>.md: critical, high, normal (default), low; ties sort
by task number.

On a branch, the first line names the tree and how far behind main it is. A
task archived on main but still present here is listed as landed: merge main.
An effort whose spec declares **Integration branch:** B lands on B instead:
on B or its task branches, B's archives count as landed; on any other branch
the effort is reported under "Integrating on another branch" and its tasks
are neither ready nor counted.

Started tasks are listed above the ready ones, never as ready: paused, needing
replanning (Status: blocked), in progress, landed, or finished on a branch but
not landed. A task counts as started when its file in this tree has a Status,
or when a task branch <effort>/<NN>-<slug> exists locally or on a remote
(under pr, git fetch first); the branch's copy of the task file gives its
Status. Branches that all claim one task are named last.

Options:
  --effort SLUG  Scope output to a single effort slug
  --all          Return all ready tasks instead of capping at five
  --brief        Compact snapshot; print nothing when the queue is idle
  --format TYPE  Output text (default) or JSON
  --no-color     Disable terminal colors
  -h, --help     Show this help
"""


def spec_info(wf, effort, cache):
    """(priority, rank, goal) for an effort from specs/<effort>.md."""
    if effort in cache:
        return cache[effort]
    priority, goal = "normal", "(not specified)"
    path = os.path.join(wf, "specs", effort + ".md")
    if effort != "none" and os.path.isfile(path):
        lines = read_lines(path)
        priority = read_fields(path, lines).get("priority", "normal").lower() or "normal"
        goal = spec_goal(lines) or goal
    if priority not in PRIORITY_RANK:
        sys.stderr.write("Warning: unknown priority '%s' for effort %s; using normal\n" % (priority, effort))
        priority = "normal"
    cache[effort] = (priority, PRIORITY_RANK[priority], goal)
    return cache[effort]


def spec_goal(lines):
    """First non-empty line under `## What this is for`."""
    inside = False
    for _, line in visible_lines(lines):
        text = line.strip()
        if text.startswith("#"):
            if inside:
                return ""
            m = H2_RE.match(text)
            if m and m.group(1).strip().rstrip(":").strip().lower() == "what this is for":
                inside = True
            continue
        if inside and text:
            return text
    return ""


def spec_check_ids(wf, effort):
    """Check IDs ({'W1', ...}) the effort's spec defines, or an empty set.

    IDs are the bolded labels of the bullets under `## How we will know it
    works`: `- **W1.** <what command, what result>`. A spec without that section,
    or one that lists no IDs, yields an empty set and disables the check.
    """
    path = os.path.join(wf, "specs", effort + ".md")
    if effort == "none" or not os.path.isfile(path):
        return set()
    found = set()
    inside = False
    for _, line in visible_lines(read_lines(path)):
        text = line.strip()
        if text.startswith("#"):
            m = H2_RE.match(text)
            if m:
                title = m.group(1).strip().rstrip(":").strip().lower()
                inside = title == "how we will know it works"
            continue
        if inside:
            m = SPEC_CHECK_RE.match(text)
            if m:
                found.add(m.group(1))
    return found


# A started task's file lives on its branch until it lands (CONVENTIONS §7), so
# `next` reads task branches to tell claimed, paused and blocked tasks from ready ones.
TASK_BRANCH_RE = re.compile(r"^([a-z0-9]+(?:-[a-z0-9]+)*)/([0-9]+)-([^/]+)$")
STARTED_ORDER = {"paused": 0, "blocked": 1, "active": 2, "landed": 3, "finished": 4}


def git_out(root, *args):
    """stdout of a git command run in root, or None when it fails."""
    try:
        proc = subprocess.run(["git", "-C", root] + list(args), stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, universal_newlines=True)
    except OSError:
        return None
    return proc.stdout if proc.returncode == 0 else None


def git_cat_batch(root, specs, check_only=False):
    """Run `git cat-file --batch` (or `--batch-check`) once across `specs`.

    Returns `{spec: (oid, obj_type, body_bytes_or_None)}` for existing objects.
    """
    if not specs:
        return {}
    unique = list(dict.fromkeys(specs))
    flag = "--batch-check" if check_only else "--batch"
    payload = ("\n".join(unique) + "\n").encode("utf-8")
    try:
        proc = subprocess.run(["git", "-C", root, "cat-file", flag],
                              input=payload, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL)
    except OSError:
        return {}
    if proc.returncode != 0:
        return {}
    raw = proc.stdout
    pos = 0
    out = {}
    for spec in unique:
        nl = raw.find(b"\n", pos)
        if nl < 0:
            break
        header = raw[pos:nl].decode("utf-8", "replace")
        pos = nl + 1
        parts = header.split()
        if header.endswith(" missing") and (len(parts) < 3 or not parts[2].isdigit()):
            continue
        if len(parts) < 3 or not parts[2].isdigit():
            continue
        oid, obj_type, size = parts[0], parts[1], int(parts[2])
        if check_only:
            out[spec] = (oid, obj_type, None)
        else:
            body = raw[pos:pos + size]
            pos += size + 1
            out[spec] = (oid, obj_type, body)
    return out


def strip_remote_prefix(rest, remotes):
    """Strip the remote prefix from refs/remotes/<rest>, supporting multi-slash remotes."""
    for remote in remotes:
        prefix = remote + "/"
        if rest.startswith(prefix):
            return rest[len(prefix):]
    parts = rest.split("/")
    if len(parts) >= 3:
        return "/".join(parts[-2:])
    return parts[1] if len(parts) == 2 else ""


def list_branch_refs(root):
    """[(refname, branch_name, commit_oid, is_local)] with local branches first, newest first."""
    listing = git_out(root, "for-each-ref", "--sort=-committerdate",
                      "--format=%(refname)\t%(objectname)", "refs/heads", "refs/remotes")
    if not listing:
        return []
    remotes = None
    heads, remote_refs = [], []
    for row in listing.splitlines():
        if "\t" not in row:
            continue
        refname, oid = row.split("\t", 1)
        oid = oid.strip()
        if refname.startswith("refs/heads/"):
            heads.append((refname, refname[len("refs/heads/"):], oid, True))
        elif refname.startswith("refs/remotes/"):
            if remotes is None:
                raw_remotes = (git_out(root, "remote") or "").splitlines()
                remotes = sorted((r.strip() for r in raw_remotes if r.strip()), key=len, reverse=True)
            name = strip_remote_prefix(refname[len("refs/remotes/"):], remotes)
            if name and name != "HEAD" and not name.endswith("/HEAD"):
                remote_refs.append((refname, name, oid, False))
    return heads + remote_refs


def parse_done_tree(root, tree_oid, tree_cache):
    """Set of archived task refs inside a `.workflow/done` git tree OID."""
    if tree_oid in tree_cache:
        return tree_cache[tree_oid]
    found = set()
    listing = git_out(root, "ls-tree", "-r", tree_oid)
    if listing:
        flat_blobs = {}
        for line in listing.splitlines():
            parts = line.split(None, 2)
            if len(parts) < 3 or parts[1] != "blob":
                continue
            rest = parts[2].split("\t", 1)
            if len(rest) != 2:
                continue
            oid, rel = rest[0].strip(), rest[1].strip()
            path_parts = rel.split("/")
            if len(path_parts) >= 2:
                m = TASK_FILE_RE.match(path_parts[-1])
                if m:
                    found.add(task_ref(path_parts[0], norm_number(m.group(1))))
            elif len(path_parts) == 1:
                m = TASK_FILE_RE.match(path_parts[0])
                if m:
                    flat_blobs[oid] = norm_number(m.group(1))
        if flat_blobs:
            blobs = git_cat_batch(root, list(flat_blobs.keys()), check_only=False)
            for oid, number in flat_blobs.items():
                entry = blobs.get(oid)
                if entry and entry[2]:
                    meta = read_fields("", lines=split_text(entry[2].decode("utf-8", "replace")))
                    effort = meta.get("effort")
                    if effort and SLUG_RE.match(effort):
                        found.add(task_ref(effort, number))
    tree_cache[tree_oid] = found
    return found


def batch_done_by_commit(root, oids, done_cache, tree_cache):
    """Populate `done_cache[oid]` -> archived task refs for a batch of commit OIDs."""
    missing = [oid for oid in dict.fromkeys(oids) if oid and oid not in done_cache]
    if not missing:
        return
    specs = ["%s:.workflow/done" % oid for oid in missing]
    checked = git_cat_batch(root, specs, check_only=True)
    for oid in missing:
        entry = checked.get("%s:.workflow/done" % oid)
        if entry and entry[1] == "tree":
            done_cache[oid] = parse_done_tree(root, entry[0], tree_cache)
        else:
            done_cache[oid] = set()


def batch_branch_task_statuses(root, candidates, done_cache):
    """Return `{(task_ref, branch_name): status}` for `(task_ref, branch_name, oid)` items."""
    if not candidates:
        return {}
    specs = []
    item_paths = []
    for t_ref, b_name, oid in candidates:
        effort, rest = b_name.split("/", 1)
        p1 = "%s:.workflow/tasks/%s/%s.md" % (oid, effort, rest)
        p2 = "%s:.workflow/tasks/%s.md" % (oid, rest)
        p_done = "%s:.workflow/done/%s/%s.md" % (oid, effort, rest)
        specs.extend((p1, p2, p_done))
        item_paths.append((t_ref, b_name, oid, p1, p2, p_done))
    blobs = git_cat_batch(root, specs, check_only=False)
    out = {}
    for t_ref, b_name, oid, p1, p2, p_done in item_paths:
        found_task = blobs.get(p1) or blobs.get(p2)
        if found_task and found_task[1] == "blob":
            text = found_task[2].decode("utf-8", "replace")
            lines = [line[:-1] if line.endswith("\r") else line for line in text.split("\n")]
            status = read_fields(p1, lines).get("status").lower()
            out[(t_ref, b_name)] = status if status in ("paused", "blocked") else "active"
        elif p_done in blobs or t_ref in done_cache.get(oid, ()):
            out[(t_ref, b_name)] = "finished"
        else:
            out[(t_ref, b_name)] = "active"
    return out


def is_ancestor_commit(root, ancestor_oid, descendant_oid, cache):
    """True when `ancestor_oid` is already merged into (or equal to) `descendant_oid`."""
    if not ancestor_oid or not descendant_oid:
        return False
    if ancestor_oid == descendant_oid:
        return True
    key = (ancestor_oid, descendant_oid)
    if key not in cache:
        ok, _ = git_result(root, "merge-base", "--is-ancestor", ancestor_oid, descendant_oid)
        cache[key] = ok
    return cache[key]


def current_branch(root):
    """The checked-out branch, or None on a detached HEAD."""
    name = (git_out(root, "symbolic-ref", "-q", "--short", "HEAD") or "").strip()
    return name or None

def main_branch(root):
    """`main` if it exists, else `master`, else None."""
    for name in ("main", "master"):
        if git_out(root, "rev-parse", "--verify", "--quiet", "--end-of-options",
                   "refs/heads/" + name) is not None:
            return name
    return None


def resolve_branch(root, name):
    """The refname that resolves `name` locally or as origin/`name`, or None."""
    for candidate in (name, "origin/" + name):
        if git_out(root, "rev-parse", "--verify", "--quiet", "--end-of-options",
                   candidate + "^{commit}") is not None:
            return candidate
    return None


def tree_info(root):
    """Where this working tree sits relative to the main branch."""
    branch = current_branch(root)
    head = (git_out(root, "rev-parse", "--short", "HEAD") or "").strip() or None
    main = main_branch(root)
    behind = 0
    if main and branch != main and head:
        count = (git_out(root, "rev-list", "--count", "HEAD.." + main) or "").strip()
        if count.isdigit():
            behind = int(count)
    return {"branch": branch, "head": head, "main": main, "behind_main": behind}


def tree_header(tree):
    """The first `next` line: which tree is being read and how stale it is."""
    main = tree["main"]
    if not main or tree["branch"] == main or not tree["head"]:
        return ""
    label = tree["branch"] or "detached HEAD"
    if tree["behind_main"]:
        return "Reading %s (%s); %s is %d commit(s) ahead.\n" % (
            label, tree["head"], main, tree["behind_main"])
    return "Reading %s (%s).\n" % (label, tree["head"])


def branch_effort_counts(root, ref, effort):
    """(archived, open) task-file counts for one effort in a branch's tree."""
    counts = []
    for area in ("done", "tasks"):
        total = 0
        tree = git_out(root, "ls-tree", "-r", "--name-only", ref, "--",
                       ".workflow/%s/%s" % (area, effort))
        for rel in (tree or "").splitlines():
            parts = rel.split("/")
            if (len(parts) >= 4 and parts[0] == ".workflow" and parts[1] == area
                    and parts[2] == effort and TASK_FILE_RE.match(parts[-1])):
                total += 1
        counts.append(total)
    return counts[0], counts[1]


PATH_CONNECTORS = {"and", "or", "plus", "&", "+"}
FILE_EXT_RE = re.compile(r"^[A-Za-z0-9_.-]+\.[A-Za-z][A-Za-z0-9_+-]*$")
DOTFILE_RE = re.compile(r"^\.[A-Za-z][A-Za-z0-9._-]*$")
FILES_NONE_RE = re.compile(r"^(none|n/a|na)$", re.IGNORECASE)


def _clean_path_token(root, token):
    token = token.strip().strip("`'\"").strip()
    if token.endswith((".", ":", ";")) and not token.endswith(".."):
        token = token.rstrip(".:;").strip("`'\"").strip()
    if (not token or FILES_NONE_RE.match(token)
            or (token.startswith("<") and token.endswith(">"))):
        return ""
    if " " in token or "\t" in token or "|" in token:
        return ""
    while token.startswith("./"):
        token = token[2:]
    if "*" in token:
        prefix = token.split("*", 1)[0].rstrip("/")
        token = prefix if prefix else token
    token = token.rstrip("/")
    if not token or token in (".", "..") or token.lower() in PATH_CONNECTORS:
        return ""
    if "/" in token or DOTFILE_RE.match(token) or FILE_EXT_RE.match(token):
        return token
    if os.path.exists(os.path.join(root, token)):
        return token
    return ""


def parse_task_files_field(root, raw):
    """Normalized path list extracted from a task's `Files:` field."""
    if not raw or not raw.strip():
        return []
    cleaned = re.sub(r"\([^)`]*\)", " ", raw)
    out = []

    def add(candidate):
        norm = _clean_path_token(root, candidate)
        if norm and norm not in out:
            out.append(norm)

    def resolve_segment(segment):
        parts = [p for p in re.findall(r"`[^`]+`|\S+", segment)
                 if p.strip("`'\"").lower() not in PATH_CONNECTORS]
        resolved = [_clean_path_token(root, p) for p in parts]
        if parts and all(resolved):
            for norm in resolved:
                if norm not in out:
                    out.append(norm)
        else:
            for quoted in re.findall(r"`([^`]+)`", segment):
                add(quoted)

    if "," in cleaned or ";" in cleaned:
        for chunk in re.split(r"[,;]+", cleaned):
            if chunk.strip():
                resolve_segment(chunk)
        return out
    resolve_segment(cleaned)
    return out


def compute_file_overlaps(ready_tasks):
    """Shared paths across ready tasks (exact match or parent/child directory)."""
    overlaps = {}
    for i in range(len(ready_tasks)):
        t1 = ready_tasks[i]
        for j in range(i + 1, len(ready_tasks)):
            t2 = ready_tasks[j]
            for p1 in t1["files"]:
                for p2 in t2["files"]:
                    shared = None
                    if p1 == p2 or p2.startswith(p1 + "/"):
                        shared = p1
                    elif p1.startswith(p2 + "/"):
                        shared = p2
                    if shared:
                        overlaps.setdefault(shared, set()).update((t1["task"], t2["task"]))
    return [{"path": path, "tasks": sorted(refs)}
            for path, refs in sorted(overlaps.items(), key=lambda kv: (-len(kv[1]), kv[0]))]


def open_maps(root, wf):
    """[{"path": rel, "slug": slug, "title": title}] for active maps in .workflow/maps/."""
    maps_dir = os.path.join(wf, "maps")
    if not os.path.isdir(maps_dir):
        return []
    found = []
    for name in sorted(os.listdir(maps_dir)):
        if not name.endswith(".md"):
            continue
        path = os.path.join(maps_dir, name)
        if not os.path.isfile(path):
            continue
        lines = read_lines(path)
        if read_fields(path, lines).get("status", "").strip().lower() == "clear":
            continue
        slug = name[:-3]
        title = slug
        for _, line in visible_lines(lines):
            text = line.strip()
            if text.startswith("# ") and not text.startswith("## "):
                heading = text[2:].strip()
                if heading.lower().startswith("map:"):
                    heading = heading[4:].strip()
                if heading:
                    title = heading
                break
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        found.append({"path": rel, "slug": slug, "title": title})
    return found


def collect_queue(root, wf, target_effort=None):
    """Gather queue state across the working tree and git branches."""
    tasks_dir, done_dir = os.path.join(wf, "tasks"), os.path.join(wf, "done")
    tree = tree_info(root)
    maps = [] if target_effort else open_maps(root, wf)
    if not os.path.isdir(tasks_dir):
        return {"missing_tasks_dir": True, "tree": tree, "open_maps": maps, "ready": [],
                "started": [], "stackable": [], "block_tasks": {}, "block_numbers": {},
                "blockers": [], "task_count": 0, "blocked_count": 0, "waiting": 0,
                "integrating": [], "duplicates": [], "overlaps": []}
    # Blockers archived under done/ are finished and never block.
    archived = set()
    for path in task_files(done_dir, root=root):
        m = TASK_FILE_RE.match(os.path.basename(path))
        if not m:
            continue
        effort = read_fields(path).get("effort") or dir_effort(done_dir, path)
        if not effort:
            continue
        archived.add(task_ref(effort, norm_number(m.group(1))))

    current = tree["branch"]
    main = tree["main"]
    head_oid = (git_out(root, "rev-parse", "--verify", "--quiet", "HEAD^{commit}") or "").strip() or None
    all_refs = list_branch_refs(root)
    oid_by_ref = {}
    for refname, b_name, oid, is_local in all_refs:
        oid_by_ref.setdefault(refname, oid)
        if is_local:
            oid_by_ref.setdefault(b_name, oid)
        elif refname.startswith("refs/remotes/"):
            oid_by_ref.setdefault(refname[len("refs/remotes/"):], oid)

    def ref_oid(name):
        for candidate in (name, "origin/" + name):
            if candidate in oid_by_ref:
                return candidate, oid_by_ref[candidate]
        resolved = resolve_branch(root, name)
        if resolved:
            oid = (git_out(root, "rev-parse", "--verify", "--quiet",
                           "--end-of-options", resolved + "^{commit}") or "").strip()
            if oid:
                oid_by_ref[resolved] = oid
                return resolved, oid
        return None, None

    done_cache = {}
    tree_cache = {}
    ancestor_cache = {}

    landed_main = set()
    if main and current != main:
        _, main_oid = ref_oid(main)
        if main_oid:
            batch_done_by_commit(root, [main_oid], done_cache, tree_cache)
            landed_main = set(done_cache.get(main_oid, ()))
            archived |= landed_main

    hidden_efforts = set()
    integrating = []
    integration_landed = {}
    skip_branches = set()
    specs_dir = os.path.join(wf, "specs")
    if os.path.isdir(specs_dir):
        for name in sorted(os.listdir(specs_dir)):
            path = os.path.join(specs_dir, name)
            if not name.endswith(".md") or not os.path.isfile(path):
                continue
            effort = name[:-3]
            if target_effort and effort != target_effort:
                continue
            integration = read_fields(path).get("integration branch").strip()
            if not integration or (integration.startswith("<") and integration.endswith(">")):
                continue
            ref, int_oid = ref_oid(integration)
            if ref is None or not int_oid:
                continue
            match = TASK_BRANCH_RE.match(current) if current else None
            if current == integration or (match is not None and match.group(1) == effort):
                batch_done_by_commit(root, [int_oid], done_cache, tree_cache)
                for carried in done_cache.get(int_oid, ()):
                    if carried.split("/", 1)[0] == effort:
                        archived.add(carried)
                        integration_landed.setdefault(carried, integration)
                skip_branches.update((integration, ref))
            else:
                done_count, open_count = branch_effort_counts(root, ref, effort)
                integrating.append({"effort": effort, "branch": integration,
                                    "archived": done_count, "open": open_count})
                hidden_efforts.add(effort)

    # Scan local open task files first so we only inspect git blobs for live open tasks.
    local_tasks = []
    open_refs = set()
    base_branches_needed = []
    for path in task_files(tasks_dir, root=root):
        name = os.path.basename(path)
        m = TASK_FILE_RE.match(name)
        if not m:
            sys.stderr.write("Warning: ignoring task with invalid filename: %s\n" % name)
            continue
        number = norm_number(m.group(1))
        meta = read_fields(path)
        effort = meta.get("effort") or dir_effort(tasks_dir, path) or "none"
        if not SLUG_RE.match(effort):
            sys.stderr.write("Warning: %s has an invalid effort reference; using default priority\n" % name)
            effort = "none"
        if target_effort and effort != target_effort:
            continue
        if effort in hidden_efforts:
            continue
        status = meta.get("status").lower()
        if status in ("done", "completed", "archived"):
            continue
        ref = task_ref(effort, number)
        open_refs.add(ref)
        base_branch = meta.get("base branch").strip()
        if base_branch and not (base_branch.startswith("<") and base_branch.endswith(">")):
            base_branches_needed.append(base_branch)
        local_tasks.append((path, name, number, meta, effort, status, ref, base_branch))

    # Collect potential unlanded blocker refs referenced by open tasks (for stackable PRs).
    blocker_refs_needed = set()
    for _, _, _, meta, effort, status, ref, _ in local_tasks:
        if ref in landed_main or ref in integration_landed or status in ("active", "paused", "blocked"):
            continue
        raw = meta.get("blocked by")
        if raw:
            try:
                for token, _ in parse_blockers(raw):
                    target = resolve_ref(token, effort)
                    if target not in archived:
                        blocker_refs_needed.add(target)
            except BlockerError:
                pass

    relevant_task_refs = (open_refs - landed_main - set(integration_landed)) | blocker_refs_needed
    branches = {}
    task_branch_candidates = []
    other_branch_oids = []
    for refname, b_name, oid, _ in all_refs:
        if b_name not in skip_branches:
            other_branch_oids.append(oid)
        m = TASK_BRANCH_RE.match(b_name)
        if not m or b_name == current:
            continue
        b_ref = task_ref(m.group(1), norm_number(m.group(2)))
        if b_ref not in relevant_task_refs:
            continue
        branches.setdefault(b_ref, []).append((refname, b_name, oid))
        task_branch_candidates.append((b_ref, b_name, oid))

    base_oid_by_name = {}
    for b_name in dict.fromkeys(base_branches_needed):
        _, b_oid = ref_oid(b_name)
        if b_oid:
            base_oid_by_name[b_name] = b_oid
            other_branch_oids.append(b_oid)

    if local_tasks:
        batch_done_by_commit(root, other_branch_oids, done_cache, tree_cache)

    branch_status_cache = batch_branch_task_statuses(root, task_branch_candidates, done_cache)
    finished_branches = {}
    live_branches = {}
    for b_ref, candidates in branches.items():
        kept = []
        for refname, b_name, oid in candidates:
            b_status = branch_status_cache[(b_ref, b_name)]
            if b_status == "finished" and is_ancestor_commit(root, oid, head_oid, ancestor_cache):
                continue
            kept.append((refname, b_name, oid))
            if b_status == "finished" and b_ref not in finished_branches and b_name not in skip_branches:
                finished_branches[b_ref] = b_name
        if kept:
            live_branches[b_ref] = kept

    archived_branch = {}
    if local_tasks:
        for _, b_name, oid, _ in all_refs:
            if b_name in skip_branches:
                continue
            carried = done_cache.get(oid, ())
            if not carried:
                continue
            relevant_carried = [c for c in carried if c in open_refs and c not in archived]
            if not relevant_carried:
                continue
            if is_ancestor_commit(root, oid, head_oid, ancestor_cache):
                continue
            for c in relevant_carried:
                archived_branch.setdefault(c, b_name)

    ready, started, stackable = [], [], []
    block_tasks, block_numbers = {}, {}
    task_count = len(local_tasks)
    blocked_count = waiting = 0
    specs = {}

    def record(blocker, ref, number):
        block_tasks.setdefault(blocker, []).append(ref)
        block_numbers.setdefault(blocker, []).append(int(number))

    for path, name, number, meta, effort, status, ref, base_branch in local_tasks:
        title = meta.title or meta.get("task") or "Untitled"

        if ref in landed_main:
            started.append((STARTED_ORDER["landed"], int(number), ref, title, "landed", main, effort))
            continue
        if ref in integration_landed:
            started.append((STARTED_ORDER["landed"], int(number), ref, title, "landed",
                            integration_landed[ref], effort))
            continue

        branch = None
        if status not in ("active", "paused", "blocked"):
            if ref in finished_branches:
                status, branch = "finished", finished_branches[ref]
            elif ref in archived_branch:
                status, branch = "finished", archived_branch[ref]
            elif ref in live_branches:
                branch = live_branches[ref][0][1]
                status = branch_status_cache[(ref, branch)]
        if status in STARTED_ORDER:
            started.append((STARTED_ORDER[status], int(number), ref, title, status, branch, effort))
            if status == "blocked":
                blocked_count += 1
            continue

        stacked_done = set()
        if base_branch and base_branch in base_oid_by_name:
            stacked_done = done_cache.get(base_oid_by_name[base_branch], set())

        blocked = False
        raw = meta.get("blocked by")
        if not raw:
            blocked = True
            record("(missing Blocked by field)", ref, number)
        else:
            try:
                open_refs_needed = []
                for token, _ in parse_blockers(raw):
                    target = resolve_ref(token, effort)
                    if target not in archived and target not in stacked_done and target not in open_refs_needed:
                        open_refs_needed.append(target)
                for target in open_refs_needed:
                    record(target, ref, number)
                blocked = bool(open_refs_needed)
                if open_refs_needed and all(t in finished_branches for t in open_refs_needed):
                    stackable.append((ref, [finished_branches[t] for t in open_refs_needed]))
            except BlockerError:
                blocked = True
                sys.stderr.write("Warning: %s has malformed Blocked by: %s\n" % (name, raw))
                record("(malformed Blocked by field)", ref, number)
        if blocked:
            blocked_count += 1
            waiting += 1
            continue
        priority, rank, goal = spec_info(wf, effort, specs)
        rel_path = os.path.relpath(path, root).replace(os.sep, "/")
        branch_name = "%s/%s" % (effort, name[:-3])
        files_list = parse_task_files_field(root, meta.get("files"))
        task = {"task": ref, "number": int(number), "title": title, "effort": effort,
                "priority": priority, "goal": goal, "check": meta.get("check") or "(not specified)",
                "path": rel_path, "branch": branch_name, "files": files_list,
                "start_command": "flow-implement " + ref}
        ready.append((rank, int(number), len(ready), ref, task))
    ready.sort(key=lambda item: item[:3])
    started.sort(key=lambda item: (item[0], item[6], item[1]))
    blockers = sorted(block_tasks, key=lambda b: (-len(block_tasks[b]), b.encode("utf-8")))

    claims = {}
    for _, b_name, oid, _ in all_refs:
        m = TASK_BRANCH_RE.match(b_name)
        if not m:
            continue
        ref = task_ref(m.group(1), norm_number(m.group(2)))
        if target_effort and m.group(1) != target_effort:
            continue
        if ref not in open_refs:
            continue
        if b_name != current and branch_status_cache.get((ref, b_name)) == "finished" and is_ancestor_commit(root, oid, head_oid, ancestor_cache):
            continue
        claims.setdefault(ref, set()).add(b_name)
    duplicates = [(ref, sorted(names)) for ref, names in claims.items() if len(names) >= 2]
    duplicates.sort(key=lambda item: (-len(item[1]), item[0]))
    overlaps = compute_file_overlaps([item[4] for item in ready])
    return {"missing_tasks_dir": False, "tree": tree, "open_maps": maps, "ready": ready,
            "started": started, "stackable": stackable, "block_tasks": block_tasks,
            "block_numbers": block_numbers, "blockers": blockers, "task_count": task_count,
            "blocked_count": blocked_count, "waiting": waiting, "integrating": integrating,
            "duplicates": duplicates, "overlaps": overlaps}

def cmd_next(args):
    opts, code = parse_common(args, NEXT_USAGE, ("all", "brief", "effort"))
    if code is not None:
        return code
    root = project_root()
    wf = os.path.join(root, ".workflow")
    if not os.path.isdir(wf):
        sys.stderr.write("Error: no .workflow directory at %s; run 'workflow init' first\n" % root)
        return 1
    as_json = opts["format"] == "json"
    state = collect_queue(root, wf, opts["effort"])
    tree = state["tree"]
    header = tree_header(tree)
    maps = state["open_maps"]
    if opts["brief"] and not as_json:
        handoff = os.path.isfile(os.path.join(wf, "HANDOFF.md"))
        active_work = bool(handoff or maps or state["started"] or state["ready"]
                           or state["waiting"] or state["integrating"])
        if not active_work:
            return 0
        lines = [header]
        if handoff:
            lines.append("Handoff: .workflow/HANDOFF.md present — read it and delete once absorbed.\n")
        if maps:
            lines.append("Maps: %s (continue with flow-grill)\n" % ", ".join(m["path"] for m in maps))
        if state["started"]:
            lines.append("Started: %s\n" % ", ".join(
                "%s [%s%s]" % (item[2], item[4], " @" + item[5] if item[5] else "")
                for item in state["started"]))
        if state["ready"]:
            ready_refs = [item[3] for item in state["ready"]]
            lines.append("Ready (%d): %s\n" % (len(ready_refs), ", ".join(ready_refs)))
        if state["overlaps"]:
            lines.append("Overlaps: %s\n" % ", ".join(
                "%s (%s)" % (o["path"], "+".join(o["tasks"])) for o in state["overlaps"]))
        if state["waiting"]:
            lines.append("Waiting on dependencies: %d task(s)\n" % state["waiting"])
        if state["integrating"]:
            lines.append("Integrating elsewhere: %s\n" % ", ".join(
                "%s on %s" % (i["effort"], i["branch"]) for i in state["integrating"]))
        sys.stdout.write("".join(lines))
        return 0
    if state["missing_tasks_dir"]:
        if as_json:
            out = {"ready_count": 0, "task_count": 0, "blocked_count": 0, "ready_tasks": [],
                   "file_overlaps": [], "blockers": [], "in_progress": [], "open_maps": maps,
                   "tree": tree, "integrating": [], "duplicate_claims": []}
            sys.stdout.write(json.dumps(out, ensure_ascii=False, separators=(",", ":")) + "\n")
        else:
            extra = ""
            if maps:
                extra = "Open exploration maps (continue with flow-grill):\n" + "".join(
                    "  %s — %s\n" % (m["path"], m["title"]) for m in maps)
            sys.stdout.write(header + extra + "No tasks found (tasks directory is missing).\n")
        return 0

    ready = state["ready"]
    started = state["started"]
    stackable = state["stackable"]
    block_tasks = state["block_tasks"]
    block_numbers = state["block_numbers"]
    shown_ready = ready if opts["all"] else ready[:5]
    shown_blockers = state["blockers"] if opts["all"] else state["blockers"][:5]
    shown_duplicates = state["duplicates"] if opts["all"] else state["duplicates"][:5]
    overlaps = state["overlaps"]
    integrating = state["integrating"]
    task_count = state["task_count"]
    blocked_count = state["blocked_count"]
    waiting = state["waiting"]

    if as_json:
        out = {"ready_count": len(ready), "task_count": task_count, "blocked_count": blocked_count,
               "ready_tasks": [item[4] for item in shown_ready],
               "file_overlaps": overlaps,
               "blockers": [{"dependency": b, "count": len(block_tasks[b]), "tasks": block_numbers[b]}
                            for b in shown_blockers],
               "in_progress": [{"task": item[2], "number": item[1], "effort": item[6], "title": item[3],
                                "status": item[4], "branch": item[5]} for item in started],
               "open_maps": maps, "tree": tree, "integrating": integrating,
               "duplicate_claims": [{"task": ref, "branches": names} for ref, names in shown_duplicates]}
        sys.stdout.write(json.dumps(out, ensure_ascii=False, separators=(",", ":")) + "\n")
        return 0

    color = colors_enabled(opts["no_color"])
    bold, dim, off = ("\033[1m", "\033[2m", "\033[0m") if color else ("", "", "")
    out = [header]
    if maps:
        out.append("Open exploration maps (continue with flow-grill):\n")
        for m in maps:
            out.append("  %s%s%s — %s\n" % (bold, m["path"], off, m["title"]))
    if task_count == 0 and not integrating:
        sys.stdout.write("".join(out) + "No tasks found.\n")
        return 0
    headings = (("paused", "Paused (resume before starting new work):"),
                ("blocked", "Needs replanning (Status: blocked; flow-break amend):"),
                ("active", "In progress:"))
    for st_name, heading in headings:
        rows = [item for item in started if item[4] == st_name]
        if rows:
            out.append("%s\n" % heading)
            for item in rows:
                where = "branch %s" % item[5] if item[5] else "this working tree"
                out.append("  %s%s%s %s (%s)\n" % (bold, item[2], off, item[3], where))
    landed_branches = []
    for item in started:
        if item[4] == "landed" and item[5] not in landed_branches:
            landed_branches.append(item[5])
    for landed_branch in landed_branches:
        out.append("Landed on %s, still open in this tree (merge %s into this branch):\n"
                   % (landed_branch, landed_branch))
        for item in started:
            if item[4] == "landed" and item[5] == landed_branch:
                out.append("  %s%s%s %s (branch %s)\n" % (bold, item[2], off, item[3], landed_branch))
    finished_rows = [item for item in started if item[4] == "finished"]
    if finished_rows:
        out.append("Finished on a branch, not landed yet:\n")
        for item in finished_rows:
            out.append("  %s%s%s %s (branch %s)\n" % (bold, item[2], off, item[3], item[5]))
    if task_count:
        if ready:
            if opts["all"]:
                out.append("%sReady tasks%s (%d):\n" % (bold, off, len(ready)))
            else:
                out.append("%sReady tasks%s (%d total; showing up to 5):\n" % (bold, off, len(ready)))
            for i, item in enumerate(shown_ready, 1):
                ref, task = item[3], item[4]
                out.append("%d) %s%s%s %s (priority: %s)\n" % (i, bold, ref, off, task["title"], task["priority"]))
                if task["goal"] != "(not specified)":
                    out.append("   %sGoal:%s %s\n" % (dim, off, task["goal"]))
                out.append("   %sCheck:%s %s\n" % (dim, off, task["check"]))
                out.append("   Start: flow-implement %s\n" % ref)
            if overlaps:
                shown_overlaps = overlaps if opts["all"] else overlaps[:5]
                out.append("Shared files across ready tasks (serialize or coordinate): %s\n" % (
                    ", ".join("%s (%s)" % (o["path"], ", ".join(o["tasks"])) for o in shown_overlaps)))
        else:
            out.append("No ready tasks.\n")
    if waiting:
        out.append("Waiting on dependencies: %d task(s); top blockers: %s\n" % (
            waiting, ", ".join("%s (%d)" % (b, len(block_tasks[b])) for b in shown_blockers)))
        if stackable:
            out.append("Stackable under pr (blockers finished on branch): %s\n" % (
                ", ".join("%s (on %s)" % (r, ", ".join(bs)) for r, bs in stackable)))
    if integrating:
        out.append("Integrating on another branch (run next there):\n")
        for item in integrating:
            out.append("  %s on %s: %d archived, %d open\n" % (
                item["effort"], item["branch"], item["archived"], item["open"]))
    if shown_duplicates:
        out.append("Several branches claim one task (keep one; rename retries to "
                   "attempt/<effort>-<NN>): %s\n" % ", ".join(
                       "%s (%d)" % (ref, len(names)) for ref, names in shown_duplicates))
    sys.stdout.write("".join(out))
    return 0


TASK_USAGE = """workflow task - manage task lifecycle state transitions

Usage: workflow task start <task>
       workflow task archive <task>
       workflow task <subcommand> --help

Subcommands:
  start    record current HEAD as Base commit, set Status: active,
           and update matching spec to in progress
  archive  verify checkboxes and task proof, remove Status,
           move task to .workflow/done/<effort>/, and validate
"""

TASK_START_USAGE = """workflow task start - activate a task and record base commit

Usage: workflow task start <task>

Records the current git HEAD as Base commit, sets Status to active,
and updates the matching spec to 'in progress' if it was proposed or done.
<task> can be <effort>/<NN>, <effort>/<NN>-<slug>.md, or a task file path.
A bare <NN> works when unambiguous across efforts.
"""

TASK_ARCHIVE_USAGE = """workflow task archive - archive a completed task

Usage: workflow task archive <task>

Verifies all checklist items are ticked, verifies required task proof
(Check after: in ## Progress, or ## Findings for spike tasks),
removes Status, moves the task to .workflow/done/<effort>/, and validates
the resulting repository state. Refuses to overwrite existing archives.
<task> can be <effort>/<NN>, <effort>/<NN>-<slug>.md, or a task file path.
A bare <NN> works when unambiguous across efforts.
"""


def replace_file_atomically(path, new_content, mode=None):
    """Write new_content to a temporary file in the same directory and atomically replace path."""
    abs_path = os.path.abspath(path)
    dir_name = os.path.dirname(abs_path)

    if mode is None:
        if os.path.exists(abs_path):
            try:
                mode = stat.S_IMODE(os.stat(abs_path).st_mode)
            except OSError:
                pass
        else:
            try:
                current_umask = os.umask(0)
                os.umask(current_umask)
                mode = 0o666 & ~current_umask
            except Exception:
                mode = 0o644

    fd, temp_path = tempfile.mkstemp(prefix=".tmp_wf_", dir=dir_name)
    try:
        if mode is not None:
            if hasattr(os, "fchmod"):
                try:
                    os.fchmod(fd, mode)
                except OSError:
                    pass
            elif hasattr(os, "chmod"):
                try:
                    os.chmod(temp_path, mode)
                except OSError:
                    pass

        with os.fdopen(fd, "w", encoding="utf-8", newline="") as fh:
            fh.write(new_content)
        os.replace(temp_path, abs_path)
    except Exception:
        if os.path.exists(temp_path):
            try:
                os.remove(temp_path)
            except OSError:
                pass
        raise


def resolve_task_file(root, arg):
    """Resolve a user-provided task reference to a path under .workflow/tasks.

    Accepts:
    - Relative or absolute path to the file
    - <effort>/<NN> (e.g. core/01 or core/1)
    - <effort>/<NN>-<slug>.md or <effort>/<NN>-<slug>
    - bare <NN> (e.g. 01 or 1) when unambiguous across all efforts

    Returns (task_path, effort, number).
    Raises ValueError with a user-facing error message on failure.
    """
    wf = os.path.join(root, ".workflow")
    tasks_dir = os.path.join(wf, "tasks")
    done_dir = os.path.join(wf, "done")

    if not os.path.isdir(tasks_dir):
        raise ValueError("no .workflow/tasks directory found at %s" % root)

    def _validate_task_path_and_effort(target_path, user_ref):
        if not is_lexical_subpath(target_path, tasks_dir) or not is_subpath(target_path, tasks_dir):
            raise ValueError("task '%s' resolves outside .workflow/tasks" % user_ref)
        bn = os.path.basename(target_path)
        m = TASK_FILE_RE.match(bn)
        if not m:
            raise ValueError("file '%s' does not match task filename pattern (e.g. 01-slug.md)" % bn)
        num = norm_number(m.group(1))
        eff = dir_effort(tasks_dir, target_path) or read_fields(target_path).get("effort") or ""
        eff = eff.strip()
        if not eff or not SLUG_RE.match(eff):
            raise ValueError("task '%s' has invalid effort slug '%s'" % (user_ref, eff) if eff else "task '%s' has missing effort slug" % user_ref)
        return target_path, eff, num

    candidate_paths = [
        arg,
        os.path.join(root, arg),
        os.path.join(wf, arg),
        os.path.join(tasks_dir, arg),
    ]
    for cp in candidate_paths:
        if os.path.isfile(cp):
            abs_p = os.path.abspath(cp)
            if not is_safe_path(abs_p, root):
                raise ValueError("path '%s' escapes project root" % arg)
            if is_lexical_subpath(abs_p, done_dir) or is_subpath(abs_p, done_dir):
                raise ValueError("task '%s' is already in .workflow/done (%s)" % (
                    arg, os.path.relpath(abs_p, root)))
            if is_lexical_subpath(abs_p, tasks_dir):
                return _validate_task_path_and_effort(abs_p, arg)
            if "/" in arg or arg.endswith(".md"):
                raise ValueError("task '%s' is not under .workflow/tasks" % arg)
    all_tasks = task_files(tasks_dir, root=root)
    matches = []

    clean_arg = arg.strip()
    if clean_arg.endswith("/"):
        clean_arg = clean_arg[:-1]

    for p in all_tasks:
        bn = os.path.basename(p)
        m = TASK_FILE_RE.match(bn)
        if not m:
            continue
        num = norm_number(m.group(1))
        eff = dir_effort(tasks_dir, p) or read_fields(p).get("effort") or ""
        base_no_ext = os.path.splitext(bn)[0]

        if "/" in clean_arg:
            parts = clean_arg.split("/", 1)
            arg_eff = parts[0]
            arg_rest = parts[1]
            if arg_eff == eff:
                if arg_rest in (bn, base_no_ext):
                    matches.append((p, eff, num))
                else:
                    rest_m = TASK_FILE_RE.match(arg_rest if arg_rest.endswith(".md") else arg_rest + ".md")
                    if rest_m and norm_number(rest_m.group(1)) == num:
                        matches.append((p, eff, num))
                    elif norm_number(arg_rest.split("-")[0].split(".")[0]) == num:
                        matches.append((p, eff, num))
        else:
            if clean_arg in (bn, base_no_ext):
                matches.append((p, eff, num))
            else:
                bare_m = TASK_FILE_RE.match(clean_arg if clean_arg.endswith(".md") else clean_arg + ".md")
                if bare_m and norm_number(bare_m.group(1)) == num:
                    matches.append((p, eff, num))
                elif norm_number(clean_arg.split("-")[0].split(".")[0]) == num:
                    matches.append((p, eff, num))

    if len(matches) == 1:
        p, _, _ = matches[0]
        return _validate_task_path_and_effort(p, arg)

    if len(matches) > 1:
        refs = [os.path.relpath(p, root) for p, _, _ in matches]
        raise ValueError("ambiguous task reference '%s': matches multiple tasks (%s)" % (
            arg, ", ".join(refs)))

    if os.path.isdir(done_dir):
        done_tasks = task_files(done_dir, root=root)
        for p in done_tasks:
            bn = os.path.basename(p)
            m = TASK_FILE_RE.match(bn)
            if not m:
                continue
            num = norm_number(m.group(1))
            eff = dir_effort(done_dir, p) or read_fields(p).get("effort") or ""
            base_no_ext = os.path.splitext(bn)[0]
            matched_done = False
            if "/" in clean_arg:
                parts = clean_arg.split("/", 1)
                if parts[0] == eff:
                    if parts[1] in (bn, base_no_ext):
                        matched_done = True
                    elif norm_number(parts[1].split("-")[0].split(".")[0]) == num:
                        matched_done = True
            else:
                if clean_arg in (bn, base_no_ext) or norm_number(clean_arg.split("-")[0].split(".")[0]) == num:
                    matched_done = True
            if matched_done:
                raise ValueError("task '%s' is already in .workflow/done (%s)" % (
                    arg, os.path.relpath(p, root)))

    raise ValueError("task '%s' not found under .workflow/tasks" % arg)


def set_task_fields(content, fields_to_set):
    """Update or insert header fields in markdown content while preserving unrelated lines."""
    lines = content.splitlines(keepends=True)
    newline = "\r\n" if lines and lines[0].endswith("\r\n") else "\n"

    fence = None
    in_comment = False

    field_lines = {}
    last_top_field_idx = -1
    title_idx = -1
    first_h2_idx = -1
    has_bullet = False
    has_bold = True

    for idx, line in enumerate(lines):
        if fence is not None:
            fence, _ = next_fence_state(fence, line)
            continue
        stripped_line, in_comment = strip_comments(line, in_comment)
        fence, is_delim = next_fence_state(None, stripped_line)
        if is_delim:
            continue

        s = stripped_line.strip()
        if not s:
            continue
        if TITLE_RE.match(s) and title_idx == -1:
            title_idx = idx
            continue
        if H2_RE.match(s) or s.startswith("##"):
            if first_h2_idx == -1:
                first_h2_idx = idx
            continue

        clean = s
        prefix = ""
        bm = BULLET_RE.match(clean)
        if bm:
            prefix = line[:line.find(bm.group(1))]
            clean = bm.group(1).strip()
            has_bullet = True

        fm = BOLD_FIELD_RE.match(clean)
        bold_style = None
        if fm:
            key = fm.group(1).lower().rstrip(":").strip()
            bold_style = "bold_inside" if ":" in fm.group(1) else "bold_outside"
            has_bold = True
        else:
            fm = PLAIN_FIELD_RE.match(clean)
            if fm:
                key = fm.group(1).lower().rstrip(":").strip()
                bold_style = "plain"

        if bold_style is not None:
            if first_h2_idx == -1:
                last_top_field_idx = idx
            field_lines[key] = (idx, prefix, bold_style)

    to_insert = []
    for name, val in fields_to_set:
        key = name.lower()
        if key in field_lines:
            idx, prefix, bold_style = field_lines[key]
            if bold_style == "bold_inside":
                lines[idx] = "%s**%s:** %s%s" % (prefix, name, val, newline)
            elif bold_style == "bold_outside":
                lines[idx] = "%s**%s**: %s%s" % (prefix, name, val, newline)
            elif bold_style == "plain":
                lines[idx] = "%s%s: %s%s" % (prefix, name, val, newline)
            else:
                lines[idx] = "%s**%s:** %s%s" % (prefix, name, val, newline)
        else:
            to_insert.append((name, val))

    if to_insert:
        insert_prefix = "- " if has_bullet else ""
        formatted_inserts = []
        for name, val in to_insert:
            if has_bold:
                formatted_inserts.append("%s**%s:** %s%s" % (insert_prefix, name, val, newline))
            else:
                formatted_inserts.append("%s%s: %s%s" % (insert_prefix, name, val, newline))

        if last_top_field_idx != -1:
            ins_pos = last_top_field_idx + 1
            lines[ins_pos:ins_pos] = formatted_inserts
        elif first_h2_idx != -1:
            ins_pos = first_h2_idx
            lines[ins_pos:ins_pos] = formatted_inserts + [newline]
        elif title_idx != -1:
            ins_pos = title_idx + 1
            lines[ins_pos:ins_pos] = [newline] + formatted_inserts
        else:
            lines.extend(formatted_inserts)

    return "".join(lines)


def remove_field_from_content(content, field_name="status"):
    """Remove a header field from task content while preserving all other lines."""
    lines = content.splitlines(keepends=True)
    fence = None
    in_comment = False
    remove_indices = set()

    for idx, line in enumerate(lines):
        if fence is not None:
            fence, _ = next_fence_state(fence, line)
            continue
        stripped_line, in_comment = strip_comments(line, in_comment)
        fence, is_delim = next_fence_state(None, stripped_line)
        if is_delim:
            continue

        s = stripped_line.strip()
        if not s:
            continue
        if H2_RE.match(s) or s.startswith("##"):
            break

        clean = s
        bm = BULLET_RE.match(clean)
        if bm:
            clean = bm.group(1).strip()

        fm = BOLD_FIELD_RE.match(clean) or PLAIN_FIELD_RE.match(clean)
        if fm:
            key = fm.group(1).lower().rstrip(":").strip()
            if key == field_name.lower():
                remove_indices.add(idx)
                j = idx + 1
                while j < len(lines):
                    next_s = lines[j]
                    if next_s.startswith(("  ", "\t")) and next_s.strip():
                        remove_indices.add(j)
                        j += 1
                    else:
                        break

    if not remove_indices:
        return content

    out = []
    for idx, line in enumerate(lines):
        if idx in remove_indices:
            continue
        if line.strip() == "" and out and out[-1].strip() == "":
            continue
        out.append(line)

    return "".join(out)


def update_matching_spec_for_start(root, effort):
    """Advance matching spec to 'in progress' if currently proposed or done."""
    if not effort:
        return None, False
    if not SLUG_RE.match(effort):
        raise ValueError("invalid effort slug '%s'" % effort)

    wf = os.path.join(root, ".workflow")
    specs_dir = os.path.join(wf, "specs")
    if os.path.exists(specs_dir) and not is_subpath(specs_dir, wf):
        raise ValueError("specs directory resolves outside .workflow")

    spec_path = os.path.join(specs_dir, "%s.md" % effort)
    if not os.path.lexists(spec_path):
        return None, False

    if not is_lexical_subpath(spec_path, specs_dir) or not is_subpath(spec_path, specs_dir):
        raise ValueError("spec file '%s.md' resolves outside .workflow/specs" % effort)

    if not os.path.isfile(spec_path):
        return None, False

    meta = read_fields(spec_path)
    cur_status = meta.get("status", "").strip().lower()
    if cur_status in ("proposed", "done"):
        with open(spec_path, "r", encoding="utf-8", newline="") as fh:
            content = fh.read()
        new_content = set_task_fields(content, [("Status", "in progress")])
        replace_file_atomically(spec_path, new_content)
        return spec_path, True
    return spec_path, False


def task_start(task_arg):
    root = project_root()
    wf = os.path.join(root, ".workflow")
    if not os.path.isdir(wf):
        sys.stderr.write("Error: no .workflow directory at %s; run 'workflow init' first\n" % root)
        return 1

    ready, out = git_result(root, "rev-parse", "--verify", "HEAD")
    if not ready or not out.strip():
        sys.stderr.write("Error: cannot determine HEAD commit (no git commits found)\n")
        sys.stderr.write("  Fix: Make an initial commit before starting tasks.\n")
        return 1
    head_sha = out.strip()

    try:
        task_path, effort, number = resolve_task_file(root, task_arg)
    except ValueError as e:
        sys.stderr.write("Error: %s\n" % str(e))
        return 1

    if not effort or not SLUG_RE.match(effort):
        sys.stderr.write("Error: invalid effort slug '%s'\n" % effort)
        return 1

    specs_dir = os.path.join(wf, "specs")
    if os.path.exists(specs_dir) and not is_subpath(specs_dir, wf):
        sys.stderr.write("Error: specs directory resolves outside .workflow\n")
        return 1

    spec_path = os.path.join(specs_dir, "%s.md" % effort)
    if os.path.lexists(spec_path):
        if not is_lexical_subpath(spec_path, specs_dir) or not is_subpath(spec_path, specs_dir):
            sys.stderr.write("Error: spec '%s.md' resolves outside .workflow/specs\n" % effort)
            return 1

    try:
        with open(task_path, "r", encoding="utf-8", newline="") as fh:
            original_content = fh.read()
    except OSError as e:
        sys.stderr.write("Error: failed to read task file: %s\n" % str(e))
        return 1

    new_content = set_task_fields(original_content, [
        ("Base commit", head_sha),
        ("Status", "active"),
    ])

    try:
        replace_file_atomically(task_path, new_content)
    except OSError as e:
        sys.stderr.write("Error: failed to update task file: %s\n" % str(e))
        return 1

    try:
        spec_path, spec_updated = update_matching_spec_for_start(root, effort)
    except Exception as e:
        try:
            replace_file_atomically(task_path, original_content)
        except OSError:
            pass
        sys.stderr.write("Error: failed to update spec '%s.md': %s\n" % (effort, str(e)))
        return 1

    ref = task_ref(effort, number)
    extra = ", spec: in progress" if spec_updated else ""
    sys.stdout.write("Started task %s (Base commit: %s, Status: active%s)\n" % (
        ref, head_sha[:10], extra))
    return 0


def task_archive(task_arg):
    root = project_root()
    wf = os.path.join(root, ".workflow")
    if not os.path.isdir(wf):
        sys.stderr.write("Error: no .workflow directory at %s; run 'workflow init' first\n" % root)
        return 1

    try:
        task_path, effort, number = resolve_task_file(root, task_arg)
    except ValueError as e:
        sys.stderr.write("Error: %s\n" % str(e))
        return 1

    if not effort or not SLUG_RE.match(effort):
        sys.stderr.write("Error: invalid effort slug '%s'\n" % effort)
        return 1

    try:
        with open(task_path, "r", encoding="utf-8", newline="") as fh:
            original_content = fh.read()
    except OSError as e:
        sys.stderr.write("Error: failed to read task file: %s\n" % str(e))
        return 1

    try:
        source_mode = stat.S_IMODE(os.stat(task_path).st_mode)
    except OSError:
        source_mode = None

    lines = original_content.splitlines()
    shown = visible_lines(lines)

    for lineno, line in shown:
        m = UNTICKED_RE.match(line.strip())
        if m:
            item = m.group(1).strip()
            if len(item) > 60:
                item = item[:57] + "..."
            sys.stderr.write("Error: task has unticked checkbox at line %d: %s\n" % (lineno, item))
            sys.stderr.write("  Fix: Tick it with '- [x]' if done, or remove it before archiving.\n")
            return 1

    meta = read_fields(task_path, lines=lines)
    is_spike = meta.get("task type").strip().lower() == "spike"
    if is_spike:
        if not section_has_content(shown, "findings"):
            sys.stderr.write("Error: spike task missing ## Findings section\n")
            sys.stderr.write("  Fix: Record what the spike found under ## Findings before archiving.\n")
            return 1
    else:
        if not check_after_in_progress(shown):
            sys.stderr.write("Error: task missing 'Check after:' in ## Progress\n")
            sys.stderr.write("  Fix: Run the Check on the final code and record `Check after:` in ## Progress before archiving.\n")
            return 1

    done_dir = os.path.join(wf, "done")
    if os.path.exists(done_dir) and not is_subpath(done_dir, wf):
        sys.stderr.write("Error: .workflow/done resolves outside .workflow\n")
        return 1

    dest_dir = os.path.join(done_dir, effort)
    if not is_lexical_subpath(dest_dir, done_dir):
        sys.stderr.write("Error: destination directory '%s' is not under .workflow/done\n" % os.path.relpath(dest_dir, root))
        return 1

    if os.path.exists(dest_dir) or os.path.islink(dest_dir):
        if not is_subpath(dest_dir, done_dir):
            sys.stderr.write("Error: destination directory '%s' resolves outside .workflow/done\n" % os.path.relpath(dest_dir, root))
            return 1

    dest_path = os.path.join(dest_dir, os.path.basename(task_path))
    if not is_lexical_subpath(dest_path, done_dir):
        sys.stderr.write("Error: destination path '%s' is not under .workflow/done\n" % os.path.relpath(dest_path, root))
        return 1

    if os.path.exists(dest_path) or os.path.islink(dest_path):
        if not is_subpath(dest_path, done_dir):
            sys.stderr.write("Error: destination path '%s' resolves outside .workflow/done\n" % os.path.relpath(dest_path, root))
            return 1
        sys.stderr.write("Error: destination file already exists: %s\n" % os.path.relpath(dest_path, root))
        sys.stderr.write("  Fix: Remove or rename the existing archive before archiving this task.\n")
        return 1

    updated_content = remove_field_from_content(original_content, "status")
    try:
        os.makedirs(dest_dir, exist_ok=True)
    except OSError as e:
        sys.stderr.write("Error: cannot create archive directory '%s': %s\n" % (os.path.relpath(dest_dir, root), str(e)))
        return 1

    if not is_subpath(dest_dir, done_dir):
        sys.stderr.write("Error: destination directory '%s' resolves outside .workflow/done\n" % os.path.relpath(dest_dir, root))
        return 1

    try:
        replace_file_atomically(dest_path, updated_content, mode=source_mode)
    except OSError as e:
        sys.stderr.write("Error: failed to write archive destination '%s': %s\n" % (os.path.relpath(dest_path, root), str(e)))
        return 1

    try:
        os.remove(task_path)
    except OSError as e:
        if os.path.exists(dest_path):
            try:
                os.remove(dest_path)
            except OSError:
                pass
        sys.stderr.write("Error: failed to remove original task file: %s\n" % str(e))
        return 1

    validator = Validator(root)
    validator.run()

    dest_rel = os.path.relpath(dest_path, root).replace(os.sep, "/")
    new_archive_issues = [i for i in validator.issues if i["file"] == dest_rel]
    if validator.errors > 0 or new_archive_issues:
        try:
            if os.path.exists(dest_path):
                os.remove(dest_path)
        except OSError:
            pass
        try:
            replace_file_atomically(task_path, original_content, mode=source_mode)
        except OSError:
            pass

        all_fail_issues = [i for i in validator.issues if i["severity"] == "ERROR" or i["file"] == dest_rel]
        sys.stderr.write("Error: validation failed after archiving task (%d error(s)):\n" % len(all_fail_issues))
        for item in all_fail_issues:
            sys.stderr.write("  %s:%d: %s\n    Fix: %s\n" % (
                item["file"], item["line"], item["message"], item["suggestion"]))
        return 1

    ref = task_ref(effort, number)
    sys.stdout.write("Archived task %s to %s\n" % (ref, os.path.relpath(dest_path, root)))
    return 0


def cmd_task(args):
    if not args or args[0] in ("-h", "--help"):
        (sys.stdout if args else sys.stderr).write(TASK_USAGE)
        return 0 if args else 2

    subcommand = args[0]
    sub_args = args[1:]

    if subcommand in ("-h", "--help"):
        sys.stdout.write(TASK_USAGE)
        return 0

    if subcommand == "start":
        if not sub_args:
            sys.stderr.write("Error: missing task argument\n")
            sys.stderr.write(TASK_START_USAGE)
            return 2
        if sub_args[0] in ("-h", "--help"):
            sys.stdout.write(TASK_START_USAGE)
            return 0
        if len(sub_args) > 1:
            sys.stderr.write("Error: unexpected extra argument: %s\n" % sub_args[1])
            return 2
        return task_start(sub_args[0])

    if subcommand == "archive":
        if not sub_args:
            sys.stderr.write("Error: missing task argument\n")
            sys.stderr.write(TASK_ARCHIVE_USAGE)
            return 2
        if sub_args[0] in ("-h", "--help"):
            sys.stdout.write(TASK_ARCHIVE_USAGE)
            return 0
        if len(sub_args) > 1:
            sys.stderr.write("Error: unexpected extra argument: %s\n" % sub_args[1])
            return 2
        return task_archive(sub_args[0])

    sys.stderr.write("Error: unknown task subcommand: %s\n" % subcommand)
    sys.stderr.write(TASK_USAGE)
    return 2

# ------------------------------------------------------------------- main

MAIN_USAGE = """workflow-core.py %s - validate, queue and task lifecycle for the workflow toolkit

Usage: workflow-core.py validate [--strict] [--audit] [--staged] [--format text|json] [--no-color]
       workflow-core.py next [--effort SLUG] [--all] [--brief] [--format text|json] [--no-color]
       workflow-core.py task start <task>
       workflow-core.py task archive <task>
       workflow-core.py <command> --help
""" % VERSION


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        (sys.stdout if argv else sys.stderr).write(MAIN_USAGE)
        return 0 if argv else 2
    command, args = argv[0], argv[1:]
    if command == "validate":
        return cmd_validate(args)
    if command == "next":
        return cmd_next(args)
    if command == "task":
        return cmd_task(args)
    if command == "pulse":
        sys.stderr.write("workflow pulse was removed: run 'workflow next --brief'\n")
        return 2
    sys.stderr.write("Error: unknown command: %s\n" % command)
    sys.stderr.write(MAIN_USAGE)
    return 2

if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except BrokenPipeError:
        sys.exit(0)
    except KeyboardInterrupt:
        sys.exit(130)
