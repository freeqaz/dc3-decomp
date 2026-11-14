#!/usr/bin/env python3
"""pose_python.py -- which Python the game will run the pose tracker under.

The game chooses it itself (NativeSkeletonProvider::ChoosePosePython,
native/src/platform/Skeleton_Native.cpp); this is the same order, so that
preflight checks the interpreter the game is going to use rather than one of
its own choosing.  Keep the two in step: PosePythonChoiceTest (C++) and
PosePythonTest (tests/test_playtest_kit.py) run the same cases.

  1. $DC3_POSE_PYTHON, if set
  2. ${DC3_POSE_VENV:-~/.local/share/dc3/pose-venv}/bin/python3 (what
     scripts/playtest/bootstrap.sh --fix makes); DC3_POSE_VENV replaces the
     default rather than adding to it
  3. the checkout's .venv/bin/python3
  4. in a git worktree, the main checkout's .venv/bin/python3
  5. python3 from PATH

Only an executable file counts.  Whether it has the packages is not part of
the choice: the game reports that (and names bootstrap.sh --fix) when the
server exits, and preflight checks it before the game starts.

  pose_python.py [--why]     print the choice (and the reason)

Standard library only.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def _runnable(path):
    return os.path.isfile(path) and os.access(path, os.X_OK)


def main_checkout_of(root):
    """The main checkout of a git worktree at `root`, or "" (also for the
    main checkout itself).  Reads `.git` ("gitdir: <main>/.git/worktrees/x")."""
    try:
        with open(os.path.join(root, ".git")) as f:
            line = f.readline().strip()
    except OSError:  # a directory (the main checkout) or no .git at all
        return ""
    if not line.startswith("gitdir:"):
        return ""
    gitdir = line[len("gitdir:"):].strip()
    mark = "/.git/worktrees/"
    i = gitdir.find(mark)
    return gitdir[:i] if i > 0 else ""


def choose(tree_root=REPO, home=None, env=None):
    """(path, why) the game would pick."""
    env = os.environ if env is None else env
    if home is None:
        home = env.get("HOME", "")
    py = env.get("DC3_POSE_PYTHON", "")
    if py:
        return py, "DC3_POSE_PYTHON"
    venv_env = env.get("DC3_POSE_VENV", "")
    venv = venv_env or (os.path.join(home, ".local/share/dc3/pose-venv") if home else "")
    if venv and _runnable(os.path.join(venv, "bin/python3")):
        return (os.path.join(venv, "bin/python3"),
                "the playtest venv" + (" (DC3_POSE_VENV)" if venv_env else "")
                + "; scripts/playtest/bootstrap.sh --fix makes it")
    if tree_root:
        if _runnable(os.path.join(tree_root, ".venv/bin/python3")):
            return os.path.join(tree_root, ".venv/bin/python3"), "the checkout's .venv"
        main = main_checkout_of(tree_root)
        if main and _runnable(os.path.join(main, ".venv/bin/python3")):
            return (os.path.join(main, ".venv/bin/python3"),
                    "the main checkout's .venv (this is a worktree)")
    return "python3", (f"no venv found ({venv or 'no $HOME'}, {tree_root or '?'}/.venv): "
                       "python3 from PATH")


if __name__ == "__main__":
    path, why = choose()
    print(f"{path} ({why})" if "--why" in sys.argv[1:] else path)
