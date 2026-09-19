# shell-tutor

Learn the Unix shell by doing. `shell-tutor` walks you through 25 short lessons — moving around, reading files, making and deleting them, redirection, pipes, searching, scripts, and job control — and after each explanation asks you to type a real command.

Your commands run in a scratch folder that shell-tutor fills with example files, so nothing you try can touch your own files. A check runs after every command to see whether the task was done. Job-control lessons (Ctrl-Z, `fg`, `bg`, `&`, `kill %1`) can't be exercised inside a scratch shell, so those are short quizzes.

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

At the end of each section, three lessons from the two previous sections come back as a quick review — task only, no explanation. Miss one twice (or `idk` it) and it goes back into the pool to be taught again. Once every lesson is done there's a final review of all 25 in random order; running `shell-tutor` again after that repeats the final review.

Each command you type runs in a fresh `zsh`, so `cd` doesn't carry over to the next line — lessons that need it ask you to combine commands with `;`. A command that runs longer than 15 seconds is stopped.

## Lessons

1. Getting around — `pwd`, `ls`, `ls -la`, `cd`
2. Reading files — `cat`, `head`, `tail`
3. Making and changing files — `mkdir`, `cp`, `mv`, `rm`
4. Redirection — `>`, `>>`
5. Pipes — `|`, `grep`, `wc`, `sort`, `uniq`
6. Searching — `find`, `grep -r`
7. Scripts — `chmod +x`, `./`, variables, `$( )`
8. Combining commands — `;`, `&&`, `||`
9. Jobs — Ctrl-Z, `fg`, `bg`, `jobs`, `&`, `kill %1`
