# shell-tutor

Learn the Unix shell by doing. `shell-tutor` walks you through 43 short lessons (then `shell-tutor --advanced` through 35 more, and `shell-tutor --github` through 25 on git and GitHub) — moving around, reading files, making and deleting them, redirection, pipes, searching, scripts, and job control — and after each explanation asks you to type a real command.

Your commands run in a scratch directory that shell-tutor fills with example files, so nothing you try can touch your own files. A check runs after every command to see whether the task was done. Job-control lessons (Ctrl-Z, `fg`, `bg`, `&`, `kill %1`) can't be exercised inside a scratch shell, so those are short quizzes.

## Install

```bash
tlib install knittingCat/shell-tutor
```

Or build it yourself — it's one C file with no dependencies:

```bash
clang shell-tutor.c -o shell-tutor
```

## Use

```
shell-tutor          continue from the first unfinished lesson
shell-tutor 13       start at lesson 13
shell-tutor --list   show the lessons and your progress
shell-tutor --reset  forget your progress
```

At the prompt, besides typing a command (or a letter in a quiz):

| | |
|---|---|
| `hint` | a nudge |
| `idk` | "I don't know" — shows one command that would do it (the scratch files are reset so you can type it yourself); in a quiz, shows the answer and moves on. A lesson you needed the answer for isn't marked done: it comes around again later. `answer` works too |
| `skip` | move on for now — skipped lessons come around again after the last one |
| `list` | all lessons and which are done |
| `goto N` | jump to lesson N |
| `quit` | leave; progress is saved in `~/.shell-tutor/progress` |

## The second tier

`shell-tutor --advanced` is a further 35 lessons for after the first course: printf and here-documents, writing scripts (arguments, `read`, `if`, `case`, functions, loops, exit status, arithmetic), environment variables, PATH, aliases and the startup file (`~/.zshrc` / `~/.bashrc`), permissions and `chmod`, regular expressions, `sed`, `awk`, processes and signals, `2>&1`, `diff`, `du`, `date`, `ssh`/`scp`. It keeps its own progress (`~/.shell-tutor/progress-advanced`); `--advanced N`, `--advanced --list` and `--advanced --reset` work as for the first course.

Two things are different at the advanced prompt: a command may span lines (a line ending in `\` continues; a here-document `<<EOF` reads until `EOF` alone on a line), and the example files are kept between tries within a lesson, so you can write a script with one command and run it with the next. `reset` gives fresh files.

## The third tier

`shell-tutor --github` is 25 lessons on git and GitHub, meant for after the other two. The scratch directory holds small repositories built fresh for each lesson and a pretend GitHub (bare repositories `remote.git` and `shared.git`), so `clone`, `push` and `pull` are real commands with real results and nothing leaves the machine. Covered: what a commit is, `init`, `status`, `add`, `commit`, `log`, `diff`, `restore`, `.gitignore`, `add -A`, `--amend`, branches, `merge`, conflicts (quiz), `stash`, remotes, `clone`, `remote -v`, `push`, `pull`, putting a project on GitHub, HTTPS vs SSH, fork and pull request, the `gh` CLI, and what must never be pushed (all quizzes where GitHub itself would be needed). Lessons that start inside a repository show its name in the prompt (`project $`). Progress is in `~/.shell-tutor/progress-github`.

The tutor runs every command in `zsh -f`, the Mac's default shell; the lessons say so where zsh and bash differ (`echo` and `\n`, the startup file).

At the end of each section, three lessons from earlier sections come back as a quick review (drawn from the nearest sections holding 10 done lessons, going back as far as lesson 1; the previous review's picks are left out) — task only, no explanation. Miss one twice (or `idk` it) and it goes back into the pool to be taught again. Once every lesson is done there's a final review of all 43 in random order; running `shell-tutor` again after that repeats the final review.

Each command you type runs in a fresh `zsh`, so `cd` doesn't carry over to the next line — lessons that need it ask you to combine commands with `;`. A command that runs longer than 15 seconds is stopped.

## Lessons

1. Getting around — `pwd`, `ls`, `ls -la`, `echo`, `;`, paths and `/`, `cd`
2. Reading files — `cat`, `head`, `tail`, `tail -f`
3. Making and changing files — `mkdir`, `cp`, `mv`, `rm`
4. Redirection — `>`, `>>`
5. Pipes — `|`, `grep`, `wc`, `sort`, `uniq`
6. Searching — `find`, `grep -r`
7. Scripts — `chmod +x`, `./`, variables, `$( )`
8. Combining commands — `;`, `&&`, `||`
9. Jobs — Ctrl-Z, `fg`, `bg`, `jobs`, `&`, `kill %1`
10. Wildcards — `*`, `touch`, `{a,b}` brace expansion
11. Errors and status — `2>`, `/dev/null`, `$?`
12. Text tools — `cut`, `tr`, `sort -n`, `tee`
13. Loops — `for`, `xargs`
14. Links and archives — `ln -s`, `tar`
15. Finding programs — `which`, `PATH`, `man`
