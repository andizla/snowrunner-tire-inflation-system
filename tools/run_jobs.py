"""run_jobs.py: the build's and the tests' jobs from one list, side by side or one at a time.

    python -I tools\\run_jobs.py <list> --log <folder> [--stop] [--serial] [--jobs N] [--timeout S] [--times]

build.bat and test.bat call it (BRIEF_fast_build.md). The list is a text file, a job a line, in the order the jobs
ran one after another before and in which their output is printed:

    name [needs=a,b] [row] [beside] [ok=2] :: command

    needs=a,b   the jobs whose files this one takes: it starts when they are done
    row         one at a time with the other row jobs, in the list's order, and only when every job that is not a
                row job has ended (a test with child processes and waits, or one that takes something of the
                machine's that another takes)
    beside      a job the row does not wait for (it writes into a folder no row job touches)
    ok=N        exit code N is no failure; the run then ends with exit code 2 where nothing failed

A line that starts with # is a comment. %NAME% in a command is the environment's value, nothing where it is not set.
The commands run through cmd in the folder this is called from, as the lines of the .bat files did.

--serial runs the very same commands one at a time in the list's order. Otherwise as many at once as the machine has
cores less one (--jobs N sets it; the environment's JOBS too). Nothing is skipped by a file's date.

Each job's output goes to a file of its own in the log folder and is printed whole, in the list's order, as soon as
every job before it is printed: the output reads as it did one after another. A job that fails prints its name and
exit code after its output, also where it wrote nothing. A job that does not end in --timeout seconds (600) is killed
with its children and fails. With --stop no job is started after a failure (the build); without it every job runs
(the tests). Exit code: 1 where a job failed, else 2 where one ended with its ok code, else 0. The standard library only.
"""
import os
import re
import subprocess
import sys
import threading
import time


class Job:
    def __init__(self, name, command, needs, row, beside, ok):
        self.name, self.command, self.needs, self.row, self.beside, self.ok = name, command, needs, row, beside, ok
        self.state = "waiting"  # waiting, running, done, skipped
        self.code = None
        self.seconds = 0.0
        self.killed = False
        self.log = None

    def failed(self):
        return self.state == "done" and (self.killed or (self.code != 0 and self.code != self.ok))


def read_list(path):
    jobs = []
    names = set()
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if " :: " not in line:
                raise ValueError("%s line %d: no ' :: ' between the job's name and its command" % (path, number))
            head, command = line.split(" :: ", 1)
            words = head.split()
            name, needs, row, beside, ok = words[0], [], False, False, None
            for w in words[1:]:
                if w.startswith("needs="):
                    needs = [n for n in w[6:].split(",") if n]
                elif w == "row":
                    row = True
                elif w == "beside":
                    beside = True
                elif w.startswith("ok="):
                    ok = int(w[3:])
                else:
                    raise ValueError("%s line %d: '%s' is no word of a job's head" % (path, number, w))
            if name in names:
                raise ValueError("%s line %d: a second job named %s" % (path, number, name))
            for n in needs:
                if n not in names:
                    raise ValueError("%s line %d: %s needs %s, which no line before it makes" % (path, number, name, n))
            names.add(name)
            jobs.append(Job(name, command.strip(), needs, row, beside, ok))
    return jobs


def expand(command):
    return re.sub(r"%([A-Za-z_][A-Za-z0-9_]*)%", lambda m: os.environ.get(m.group(1), ""), command)


def run_one(job, log_dir, index, timeout, done):
    job.log = os.path.join(log_dir, "%02d_%s.txt" % (index, job.name))
    start = time.time()
    try:
        with open(job.log, "wb") as out:
            p = subprocess.Popen(expand(job.command), shell=True, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            try:
                job.code = p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                job.killed = True
                subprocess.run("taskkill /T /F /PID %d" % p.pid, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    job.code = p.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    job.code = -1
    except OSError as e:
        job.code = -1
        with open(job.log, "ab") as out:
            out.write(("%s: could not be started: %s\r\n" % (job.name, e)).encode("utf-8", "replace"))
    job.seconds = time.time() - start
    with done:
        job.state = "done"
        done.notify_all()


def print_job(job, timeout):
    out = sys.stdout.buffer
    if job.log and os.path.exists(job.log):
        with open(job.log, "rb") as f:
            data = f.read()
        out.write(data)
        if data and not data.endswith(b"\n"):
            out.write(b"\r\n")
    if job.killed:
        out.write(("%s: did not end in %d s and was killed\r\n" % (job.name, timeout)).encode())
    elif job.failed():
        out.write(("%s: exit code %d\r\n" % (job.name, job.code)).encode())
    out.flush()


def main(argv):
    args = argv[1:]
    if not args or args[0].startswith("--"):
        print(__doc__)
        return 1
    path = args[0]
    log_dir, stop, serial, times, timeout = None, False, False, False, 600
    count = int(os.environ.get("JOBS", "0") or 0)
    i = 1
    while i < len(args):
        a = args[i]
        if a == "--log":
            i += 1
            log_dir = args[i]
        elif a == "--stop":
            stop = True
        elif a == "--serial":
            serial = True
        elif a == "--times":
            times = True
        elif a == "--jobs":
            i += 1
            count = int(args[i])
        elif a == "--timeout":
            i += 1
            timeout = int(args[i])
        else:
            print("run_jobs: '%s' is no option" % a)
            return 1
        i += 1
    try:
        jobs = read_list(path)
    except (OSError, ValueError) as e:
        print("run_jobs: %s" % e)
        return 1
    if not log_dir:
        print("run_jobs: --log <folder> is needed")
        return 1
    os.makedirs(log_dir, exist_ok=True)
    for old in os.listdir(log_dir):
        if old.endswith(".txt"):
            os.remove(os.path.join(log_dir, old))
    if serial:
        count = 1
    elif count < 1:
        count = max(1, (os.cpu_count() or 2) - 1)
    by_name = {j.name: j for j in jobs}
    done = threading.Condition()
    printed = 0
    began = time.time()
    with done:
        while True:
            failed = any(j.failed() for j in jobs)
            running = [j for j in jobs if j.state == "running"]
            if stop and failed:
                for j in jobs:
                    if j.state == "waiting":
                        j.state = "skipped"
            # what may start now
            row_running = any(j.row for j in running)
            others_left = any(not j.row and not j.beside and j.state in ("waiting", "running") for j in jobs)
            for index, j in enumerate(jobs):
                if len(running) >= count:
                    break
                if j.state != "waiting" or any(by_name[n].state != "done" for n in j.needs):
                    if serial and j.state == "waiting":
                        break  # one at a time, in the list's order
                    continue
                if any(by_name[n].failed() for n in j.needs):
                    j.state = "skipped"
                    continue
                if j.row and not serial:
                    if row_running or others_left:
                        continue
                    # the first row job that waits, no later one before it
                    if any(k.row and k.state == "waiting" for k in jobs[:index]):
                        continue
                    row_running = True
                j.state = "running"
                running.append(j)
                threading.Thread(target=run_one, args=(j, log_dir, index, timeout, done), daemon=True).start()
                if serial:
                    break
            # print what is ready, in the list's order
            while printed < len(jobs) and jobs[printed].state in ("done", "skipped"):
                if jobs[printed].state == "done":
                    print_job(jobs[printed], timeout)
                printed += 1
            if printed == len(jobs):
                break
            if not any(j.state == "running" for j in jobs):
                # nothing runs and nothing could start: what is left waits on a job that was skipped
                stuck = [j for j in jobs if j.state == "waiting"]
                if not stuck:
                    continue
                for j in stuck:
                    j.state = "skipped"
                continue
            done.wait(timeout=1.0)
    total = time.time() - began
    lines = ["%7.1f s  %s%s" % (j.seconds, j.name, "" if j.state == "done" else "  (not run)") for j in jobs]
    lines.append("%7.1f s  in all, %d at once%s" % (total, count, ", one at a time" if serial else ""))
    with open(os.path.join(log_dir, "times.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    if times:
        print("\n".join(lines))
    if any(j.failed() for j in jobs) or any(j.state == "skipped" for j in jobs):
        return 1
    return 2 if any(j.ok is not None and j.code == j.ok and j.ok != 0 for j in jobs) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
