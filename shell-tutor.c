/*
 * shell-tutor — learn the Unix shell by doing.
 *
 * Each lesson explains one idea, then asks you to type a real command.
 * The command runs in a scratch directory that shell-tutor fills with
 * example files, so nothing you do can touch your own files. A check
 * runs afterwards to see whether the task was done; hints and answers
 * are available at the prompt. Lessons about job control (Ctrl-Z, fg,
 * bg) can't be exercised inside a scratch shell, so those are quizzes.
 *
 * Builds with plain `clang shell-tutor.c -o shell-tutor`. No dependencies.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHELL "/bin/zsh"
#define COMMAND_TIMEOUT_SEC 15
#define MAX_OUTPUT_LINES 40
#define REVIEW_SIZE 3         /* earlier lessons re-asked at the end of each section */
#define REVIEW_DEPTH 2        /* ...drawn from at most this many preceding sections */
#define REVIEW_TRIES 2

/* ---------- lessons ---------- */

enum { RUN, QUIZ };

typedef struct {
    const char *id;       /* stable slug, stored in the progress file */
    int kind;
    const char *section;
    const char *title;
    const char *explain;  /* shown before the task */
    const char *task;     /* what to do */
    const char *check;    /* RUN: zsh snippet, exit 0 = passed. QUIZ: the correct letter */
    const char *hint;
    const char *answer;   /* RUN: a command that passes. QUIZ: explanation shown after answering */
    const char *options;  /* QUIZ only: choices separated by '\n' */
} Lesson;

/*
 * The check snippet runs in the scratch directory after the user's command,
 * with these variables set:
 *   $OUT  path to a file holding everything the command printed
 *   $CMD  the command text the user typed
 */
static const Lesson LESSONS[] = {
    { "pwd", RUN, "Getting around", "Where am I?",
      "The shell always has a current directory: the folder your commands act on.\n"
      "There's a three-letter command that prints its full path; its name is short\n"
      "for \"print working directory\".",
      "Find out which folder you are in.",
      "grep -qx \"$(pwd)\" \"$OUT\" && [[ \"$CMD\" == *pwd* ]]",
      "p, then w, then d.",
      "pwd", NULL },

    { "ls", RUN, "Getting around", "What's here?",
      "There's a two-letter command, short for \"list\", that shows the files and\n"
      "folders in the current directory. Give it a folder name afterwards and it\n"
      "lists that folder instead.",
      "See what is in the current directory. (One of the things in it is a folder called docs.)",
      "grep -q 'fruits.txt' \"$OUT\" && grep -q 'docs' \"$OUT\"",
      "l, then s.",
      "ls", NULL },

    { "ls-la", RUN, "Getting around", "Hidden files and details",
      "Most commands accept options: extra words that start with a dash and change\n"
      "what the command does. ls has two you'll use constantly:\n"
      "  ls -a   also shows hidden files: any file whose name starts with a dot,\n"
      "          like .secret, is normally left out of the list\n"
      "  ls -l   shows a long listing: one file per line, with its permissions,\n"
      "          owner, size and date\n"
      "Several one-letter options can share a single dash:  ls -l -t  and  ls -lt\n"
      "mean the same thing (-t sorts by date).",
      "List everything here, hidden files included, as a long listing.",
      "grep -q '\\.secret' \"$OUT\" && grep -qE '^[-d][rwx-]{9}' \"$OUT\"",
      "You need both -l and -a; they can share one dash.",
      "ls -la", NULL },

    { "cd", RUN, "Getting around", "Moving into a folder",
      "  cd FOLDER   changes the current directory to FOLDER.\n"
      "  cd ..       goes up one level.     cd   on its own goes to your home directory.\n"
      "Paths without a leading / are relative to where you are now.\n"
      "Each line you type here runs in a fresh shell, so a cd on its own would be\n"
      "forgotten immediately: put a ; after it and add a second command on the same\n"
      "line that shows where you ended up.",
      "Go into the docs folder and show the directory you are then in.",
      "grep -q '/docs$' \"$OUT\"",
      "cd docs ; pwd",
      "cd docs; pwd", NULL },

    { "cat", RUN, "Reading files", "Show a file",
      "  cat FILE   prints a file's contents. Short for \"concatenate\": given\n"
      "several files it prints them one after another.",
      "Print the contents of fruits.txt.",
      "grep -q '^banana$' \"$OUT\" && grep -q '^cherry$' \"$OUT\"",
      "cat, then the file name.",
      "cat fruits.txt", NULL },

    { "head-tail", RUN, "Reading files", "Just the start or the end",
      "Long files are easier to peek at:\n"
      "  head -n 3 FILE   first 3 lines        tail -n 3 FILE   last 3 lines\n"
      "numbers.txt holds the numbers 1 to 20, one per line.",
      "Print only the last 2 lines of numbers.txt.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf '19\\n20')\" ]",
      "tail with -n 2.",
      "tail -n 2 numbers.txt", NULL },

    { "mkdir", RUN, "Making and changing files", "Make a folder",
      "  mkdir NAME   creates a directory.\n"
      "  mkdir -p a/b/c   creates the whole chain at once.",
      "Create a folder called photos.",
      "[ -d photos ]",
      "mkdir, then the name.",
      "mkdir photos", NULL },

    { "cp", RUN, "Making and changing files", "Copy a file",
      "  cp SOURCE DEST   copies a file. DEST can be a new file name or a folder.\n"
      "  cp -r FOLDER DEST   copies a whole folder.",
      "Make a copy of fruits.txt called fruits-backup.txt.",
      "[ -f fruits-backup.txt ] && cmp -s fruits.txt fruits-backup.txt",
      "cp fruits.txt <new name>",
      "cp fruits.txt fruits-backup.txt", NULL },

    { "mv", RUN, "Making and changing files", "Rename or move",
      "  mv OLD NEW   renames a file, or moves it if NEW is a folder.\n"
      "There is no separate rename command: a rename is a move.",
      "Rename notes.txt to todo.txt.",
      "[ -f todo.txt ] && [ ! -e notes.txt ]",
      "mv, old name, new name.",
      "mv notes.txt todo.txt", NULL },

    { "rm", RUN, "Making and changing files", "Delete",
      "  rm FILE   deletes a file. There is no trash and no undo.\n"
      "  rm -r FOLDER   deletes a folder and everything inside it.\n"
      "Be careful with rm: it does exactly what you say, immediately.",
      "Delete the file called old.log.",
      "[ ! -e old.log ] && [ -f fruits.txt ]",
      "rm, then the file name.",
      "rm old.log", NULL },

    { "redirect", RUN, "Redirection", "Send output to a file",
      "Every command's output normally goes to the screen. The > sign sends it\n"
      "into a file instead, creating the file or replacing what was in it:\n"
      "  ls > listing.txt      puts the listing in a file instead of on screen.\n"
      "  echo   just prints its arguments:  echo good morning",
      "Create a file called greeting.txt containing the single word hello.",
      "[ \"$(cat greeting.txt 2>/dev/null)\" = \"hello\" ]",
      "echo hello > greeting.txt",
      "echo hello > greeting.txt", NULL },

    { "append", RUN, "Redirection", "Add to the end of a file",
      "  >>   appends instead of replacing.\n"
      "fruits.txt currently ends with cherry.",
      "Add the line   date   to the end of fruits.txt without losing what's there.",
      "[ \"$(tail -n 1 fruits.txt)\" = \"date\" ] && grep -q '^apple$' fruits.txt",
      "echo date >> fruits.txt",
      "echo date >> fruits.txt", NULL },

    { "pipe", RUN, "Pipes", "Connect two commands",
      "The | sign (a pipe) sends one command's output into the next command's\n"
      "input. That is how small tools get combined into bigger ones:\n"
      "  cat fruits.txt | grep an     shows only the lines containing \"an\".\n"
      "  grep PATTERN   keeps only lines that match PATTERN.",
      "Show only the lines of fruits.txt that contain the letter e.",
      "[[ \"$CMD\" == *'|'* ]] && grep -q '^cherry$' \"$OUT\" && ! grep -q '^banana$' \"$OUT\"",
      "cat the file, pipe it into grep e.",
      "cat fruits.txt | grep e", NULL },

    { "wc", RUN, "Pipes", "Count things",
      "  wc -l   counts lines.   wc -w   counts words.   wc -c   counts bytes.\n"
      "Given a file name it prints the count and the name; fed through a pipe it\n"
      "prints just the number.",
      "Print how many lines numbers.txt has, using a pipe so only the number appears.",
      "[[ \"$CMD\" == *'|'* ]] && [ \"$(tr -d ' ' < \"$OUT\")\" = \"20\" ]",
      "cat numbers.txt | wc -l",
      "cat numbers.txt | wc -l", NULL },

    { "sort-uniq", RUN, "Pipes", "Sort and de-duplicate",
      "  sort   puts lines in order.   uniq   drops repeated lines, but only when\n"
      "they are next to each other, so on its own it misses repeats that are apart.\n"
      "colors.txt has repeated colors in a random order.",
      "Print each color in colors.txt once, in alphabetical order.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'blue\\ngreen\\nred\\nyellow')\" ]",
      "sort colors.txt | uniq",
      "sort colors.txt | uniq", NULL },

    { "find", RUN, "Searching", "Find files by name",
      "  find . -name 'PATTERN'   searches the current directory (.) and every\n"
      "folder inside it. Quote the pattern so the shell doesn't expand the * first.",
      "Find every file whose name ends in .md, anywhere under the current directory.",
      "grep -q 'docs/readme.md' \"$OUT\" && grep -q 'docs/guide/setup.md' \"$OUT\" && ! grep -q 'fruits.txt' \"$OUT\"",
      "find . -name '*.md'",
      "find . -name '*.md'", NULL },

    { "grep-r", RUN, "Searching", "Search inside files",
      "  grep -r WORD FOLDER   looks for WORD inside every file under FOLDER and\n"
      "prints each matching line with its file name.\n"
      "  -i   ignores case.   -n   adds line numbers.",
      "Find which file under docs mentions the word install (any case).",
      "grep -q 'setup.md' \"$OUT\" && ! grep -q 'readme.md' \"$OUT\"",
      "grep -ri install docs",
      "grep -ri install docs", NULL },

    { "chmod", RUN, "Scripts", "Make a script runnable",
      "hello.sh is a shell script, but it can't be run yet: files need the\n"
      "execute permission first.\n"
      "  chmod +x FILE   adds it. A script in the current directory is then run as\n"
      "  ./NAME   (the ./ means \"the one in this directory\", since the shell only\n"
      "searches PATH otherwise).",
      "Make hello.sh executable and run it.",
      "[ -x hello.sh ] && grep -q 'Hello from a script' \"$OUT\"",
      "chmod +x hello.sh ; ./hello.sh",
      "chmod +x hello.sh; ./hello.sh", NULL },

    { "vars", RUN, "Scripts", "Variables",
      "  NAME=value   sets a shell variable (no spaces around the =).\n"
      "  $NAME        uses it. Double quotes keep spaces together: \"$NAME\".\n"
      "  $(command)   is replaced by the command's output.",
      "Set a variable to the output of   whoami   and echo   I am <that name>.",
      "grep -q \"^I am $(whoami)$\" \"$OUT\" && [[ \"$CMD\" == *'$'* ]]",
      "me=$(whoami); echo \"I am $me\"",
      "me=$(whoami); echo \"I am $me\"", NULL },

    { "and-or", RUN, "Combining commands", "; versus &&",
      "  a ; b     runs a, then b, no matter what.\n"
      "  a && b    runs b only if a succeeded.\n"
      "  a || b    runs b only if a failed.\n"
      "Commands report success by exiting with status 0.",
      "Try to cat a file that doesn't exist (nope.txt) and print   missing   only if that fails.",
      "grep -q '^missing$' \"$OUT\" && [[ \"$CMD\" == *'||'* ]]",
      "cat nope.txt || echo missing",
      "cat nope.txt 2>/dev/null || echo missing", NULL },

    { "ctrl-z", QUIZ, "Jobs", "Ctrl-Z",
      "While a command is running in the foreground, the terminal is busy: you can't\n"
      "type another command until it finishes. Ctrl-Z suspends (pauses) the running\n"
      "program and gives you the prompt back. The program is now a job, and\n"
      "  jobs   lists them.",
      "You run   sleep 60   and press Ctrl-Z. What happens?",
      "b",
      "Suspended is not the same as stopped for good.",
      "The sleep is paused mid-way, listed by jobs as \"suspended\", and you get a prompt.\n"
      "It is not killed and it does not keep counting: it is frozen until you resume it.",
      "a) sleep is killed\nb) sleep is paused and you get a prompt back\nc) sleep keeps running in the background\nd) the terminal closes" },

    { "fg-bg", QUIZ, "Jobs", "fg and bg",
      "A suspended job can be resumed two ways:\n"
      "  fg   in the foreground: you are back inside it, as if you never stopped it.\n"
      "  bg   in the background: it keeps running but you keep the prompt.\n"
      "Both take a job spec like %1 (job number 1, from jobs). On their own they\n"
      "act on the most recent job.",
      "You suspended a long copy with Ctrl-Z and want it to keep going while you do\n"
      "other things in the same terminal. Which command?",
      "c",
      "You want it running AND you want the prompt.",
      "bg resumes the job in the background: the copy continues and the prompt is yours.\n"
      "fg would also resume it, but then the terminal is busy again until it finishes.",
      "a) fg\nb) jobs\nc) bg\nd) kill %1" },

    { "ampersand", QUIZ, "Jobs", "Starting in the background",
      "Putting & after a command starts it in the background straight away:\n"
      "  ./server &\n"
      "The shell prints its job number and process id and gives you the prompt.\n"
      "Its output still lands in your terminal unless you redirect it.",
      "What does   fg %1   do?",
      "d",
      "%1 is a job spec.",
      "fg %1 brings job number 1 into the foreground. Plain fg picks the most recent\n"
      "job; %1, %2 ... choose a specific one from the jobs list.",
      "a) forks the current shell\nb) runs job 1 again from the start\nc) kills job 1\nd) brings job 1 to the foreground" },

    { "chain-suspend", QUIZ, "Jobs", "What exactly gets suspended",
      "Ctrl-Z suspends the program that is running at that instant, not your whole\n"
      "command line. If you typed   sleep 8 ; echo done   the shell itself runs\n"
      "the sequence, and the shell never suspends: it treats the suspended sleep as\n"
      "finished and moves straight on to echo.",
      "You type   sleep 8 ; echo done   and press Ctrl-Z after two seconds. What is printed?",
      "b",
      "The ; chain belongs to the shell.",
      "\"done\" appears immediately: the shell moved on to echo as soon as sleep was\n"
      "suspended. The sleep is still there as a suspended job. To suspend the whole\n"
      "sequence as one job, run it as one process: ( sleep 8 ; echo done ).",
      "a) nothing, both are suspended\nb) done, right away, and sleep is left suspended\nc) done, after the remaining 6 seconds\nd) an error" },

    { "kill", QUIZ, "Jobs", "Getting rid of a job",
      "  kill %1      asks job 1 to quit (sends SIGTERM).\n"
      "  kill -9 %1   forces it (SIGKILL) when it ignores the polite request.\n"
      "Ctrl-C does the same as kill for whatever is in the foreground.",
      "jobs shows   [1] + suspended  sleep 8   left over from earlier. You don't want it.\n"
      "Which command removes it without resuming it in the foreground?",
      "a",
      "You can kill a job by its job spec.",
      "kill %1 ends it. fg %1 would also make it go away eventually, but by resuming\n"
      "it in the foreground and waiting for it to finish.",
      "a) kill %1\nb) fg %1\nc) exit\nd) bg %1" },

    /* ---- second tier ---- */

    { "glob", RUN, "Wildcards", "Match many files at once",
      "The shell expands * before the command runs: *.txt becomes every name here\n"
      "ending in .txt, so   cat *.txt   prints all of them. ? matches one character.\n"
      "That's why the find lesson quoted its pattern: to stop this expansion.",
      "Print how many lines all the .txt files here have together (wc -l of all of them at once).\n"
      "wc prints a total line when given several files.",
      "[[ \"$CMD\" == *'*'* ]] && grep -q 'total' \"$OUT\"",
      "wc -l with a wildcard.",
      "wc -l *.txt", NULL },

    { "touch", RUN, "Wildcards", "Empty files and brace expansion",
      "  touch NAME   creates an empty file (or just updates the date of an existing one).\n"
      "  {a,b,c}   expands to each option in turn:  echo file{1,2}.txt  prints\n"
      "file1.txt file2.txt. It works anywhere in a command.",
      "Create three empty files at once: draft1.txt, draft2.txt and draft3.txt.",
      "[ -f draft1.txt ] && [ -f draft2.txt ] && [ -f draft3.txt ] && [[ \"$CMD\" == *'{'* ]]",
      "touch draft{1,2,3}.txt",
      "touch draft{1,2,3}.txt", NULL },

    { "stderr", RUN, "Errors and status", "Errors have their own stream",
      "Commands print normal output on stream 1 (stdout) and errors on stream 2\n"
      "(stderr). > only redirects stream 1; that is why an error message still shows\n"
      "on screen when you redirect. To send errors somewhere:  2> FILE\n"
      "  2>/dev/null   throws them away.  /dev/null is a file that discards everything.",
      "Run   cat nope.txt fruits.txt   so the error about nope.txt goes into a file called errors.txt\n"
      "while the fruit list still prints.",
      "grep -q '^banana$' \"$OUT\" && grep -qi 'nope.txt' errors.txt && ! grep -qi 'no such file' \"$OUT\"",
      "... 2> errors.txt",
      "cat nope.txt fruits.txt 2> errors.txt", NULL },

    { "status", RUN, "Errors and status", "Exit status",
      "Every command ends with a number: 0 means success, anything else means some\n"
      "kind of failure. The shell keeps the last one in   $?   and && and || read it.\n"
      "  ls nope ; echo $?   prints ls's error, then a non-zero number.",
      "Run   grep zzz fruits.txt   and then print its exit status on the next line.",
      "[ \"$(tail -n 1 \"$OUT\")\" = \"1\" ] && [[ \"$CMD\" == *'$?'* ]]",
      "grep zzz fruits.txt ; echo $?",
      "grep zzz fruits.txt; echo $?", NULL },

    { "cut", RUN, "Text tools", "Pick columns",
      "people.csv has lines like   ada,lovelace,1815   — fields separated by commas.\n"
      "  cut -d , -f 2 FILE   prints field 2 of each line, using , as the delimiter.\n"
      "  -f 1,3   picks several fields.",
      "Print just the birth years (the third field) from people.csv.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf '1815\\n1912\\n1906')\" ]",
      "cut with -d , and -f 3",
      "cut -d , -f 3 people.csv", NULL },

    { "tr", RUN, "Text tools", "Change characters",
      "  tr A B   replaces every A with B in whatever is piped into it. It takes\n"
      "ranges too:  tr a-z A-Z   upper-cases everything. tr reads only from a pipe\n"
      "or <, never from a file name.",
      "Print fruits.txt in upper case.",
      "grep -q '^BANANA$' \"$OUT\" && grep -q '^CHERRY$' \"$OUT\"",
      "cat fruits.txt | tr a-z A-Z",
      "cat fruits.txt | tr a-z A-Z", NULL },

    { "sort-n", RUN, "Text tools", "Sort numbers as numbers",
      "  sort   compares text, so 10 comes before 9 (1 is less than 9).\n"
      "  sort -n   compares numerically.   -r   reverses the order.\n"
      "  head -n 1   after a sort gives you the smallest or largest.",
      "Print the single largest number in scores.txt.",
      "[ \"$(cat \"$OUT\")\" = \"97\" ]",
      "sort -n, reversed, then head -n 1",
      "sort -nr scores.txt | head -n 1", NULL },

    { "tee", RUN, "Text tools", "Save and see at once",
      "  | tee FILE   writes what flows through the pipe into FILE and also passes\n"
      "it on, so you can save a result and still see it on screen.",
      "Sort fruits.txt, saving the sorted list to sorted.txt while it also prints on screen.",
      "grep -q '^apple$' \"$OUT\" && [ \"$(cat sorted.txt 2>/dev/null)\" = \"$(sort fruits.txt)\" ]",
      "sort fruits.txt | tee sorted.txt",
      "sort fruits.txt | tee sorted.txt", NULL },

    { "for", RUN, "Loops", "Do something for each file",
      "  for f in *.txt; do echo \"$f\"; done\n"
      "runs the part between do and done once per match, with $f set to each name\n"
      "in turn. Any commands can go in the middle, separated by ;.",
      "For every .md file under docs (use find to list them), print its name followed by its line count.\n"
      "Any format is fine as long as each name and its count appear.",
      "[[ \"$CMD\" == *'for '* ]] && grep -q 'readme.md' \"$OUT\" && grep -q 'setup.md' \"$OUT\" && grep -q '3' \"$OUT\"",
      "for f in $(find docs -name '*.md'); do wc -l \"$f\"; done",
      "for f in $(find docs -name '*.md'); do wc -l \"$f\"; done", NULL },

    { "xargs", RUN, "Loops", "Turn output into arguments",
      "  find ... | xargs COMMAND   runs COMMAND with everything find printed as its\n"
      "arguments, instead of feeding it as input. So   find . -name '*.md' | xargs wc -l\n"
      "counts lines in each .md file (and prints a total).",
      "Delete every .log file anywhere under the current directory in one command using find and xargs.\n"
      "There are three: old.log, logs/a.log and logs/b.log.",
      "[[ \"$CMD\" == *xargs* ]] && [ ! -e old.log ] && [ ! -e logs/a.log ] && [ -f fruits.txt ]",
      "find . -name '*.log' | xargs rm",
      "find . -name '*.log' | xargs rm", NULL },

    { "ln", RUN, "Links and archives", "Symbolic links",
      "  ln -s TARGET NAME   makes NAME a symbolic link: a small pointer to TARGET.\n"
      "Opening NAME opens TARGET.   ls -l   shows links as   NAME -> TARGET.\n"
      "Deleting the link leaves the target alone.",
      "Create a link called latest that points to docs/readme.md.",
      "[ -L latest ] && [ \"$(readlink latest)\" = \"docs/readme.md\" ]",
      "ln -s docs/readme.md latest",
      "ln -s docs/readme.md latest", NULL },

    { "tar", RUN, "Links and archives", "Bundle a folder",
      "  tar -czf NAME.tar.gz FOLDER   packs FOLDER into one compressed file.\n"
      "  tar -xzf NAME.tar.gz          unpacks it.   -t   instead of -x just lists it.\n"
      "c=create, x=extract, z=gzip, f=file name follows.",
      "Pack the docs folder into docs.tar.gz.",
      "[ -f docs.tar.gz ] && tar -tzf docs.tar.gz | grep -q 'docs/readme.md'",
      "tar -czf docs.tar.gz docs",
      "tar -czf docs.tar.gz docs", NULL },

    { "which", RUN, "Finding programs", "Where a command lives",
      "Commands are files too. The shell finds them by searching the folders listed\n"
      "in the PATH variable, in order.   which NAME   prints the one it would use.\n"
      "  echo $PATH   shows the list, separated by colons.",
      "Print the full path of the ls program.",
      "grep -q '/ls$' \"$OUT\"",
      "which ls",
      "which ls", NULL },

    { "man", QUIZ, "Finding programs", "Reading the manual",
      "  man COMMAND   opens the manual page for a command: every option, explained.\n"
      "It shows one screen at a time: space for the next page, / to search, q to quit.\n"
      "Most commands also accept   --help   for a short summary.",
      "You're inside   man ls   and want to leave it. What do you press?",
      "c",
      "The pager has a one-letter key for quitting.",
      "q quits the pager. Ctrl-C usually works too, but q is the intended way; Ctrl-Z\n"
      "would only suspend it, leaving a job behind.",
      "a) Ctrl-Z\nb) Escape\nc) q\nd) exit" },
};

#define LESSON_COUNT ((int)(sizeof LESSONS / sizeof LESSONS[0]))

/* Files every RUN lesson starts with. Runs in a fresh scratch directory. */
static const char *FIXTURES =
    "printf 'apple\\nbanana\\ncherry\\n' > fruits.txt\n"
    "seq 1 20 > numbers.txt\n"
    "printf 'buy milk\\ncall the plumber\\n' > notes.txt\n"
    "printf 'red\\nblue\\nred\\ngreen\\nblue\\nyellow\\nred\\n' > colors.txt\n"
    "printf 'old log line\\n' > old.log\n"
    "printf 'token=abc123\\n' > .secret\n"
    "printf '#!/bin/sh\\necho Hello from a script\\n' > hello.sh\n"
    "mkdir -p docs/guide\n"
    "printf '# Project\\n\\nA small project.\\n' > docs/readme.md\n"
    "printf '# Setup\\n\\nRun the installer, then INSTALL the plugin.\\n' > docs/guide/setup.md\n"
    "printf 'draft\\n' > docs/draft.txt\n"
    "printf 'ada,lovelace,1815\\nalan,turing,1912\\ngrace,hopper,1906\\n' > people.csv\n"
    "printf '42\\n7\\n97\\n10\\n9\\n' > scores.txt\n"
    "mkdir -p logs && printf 'a\\n' > logs/a.log && printf 'b\\n' > logs/b.log\n";

/* ---------- terminal helpers ---------- */

static int use_color;

static const char *c(const char *code) { return use_color ? code : ""; }
#define BOLD   c("\033[1m")
#define DIM    c("\033[2m")
#define CYAN   c("\033[36m")
#define GREEN  c("\033[32m")
#define YELLOW c("\033[33m")
#define RED    c("\033[31m")
#define RESET  c("\033[0m")

static void die(const char *msg) {
    fprintf(stderr, "shell-tutor: %s: %s\n", msg, strerror(errno));
    exit(1);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

/* Reads a line; returns NULL on end of input (Ctrl-D). */
static char *read_line(const char *prompt, char *buf, size_t size) {
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, (int)size, stdin)) {
        putchar('\n');
        return NULL;
    }
    return trim(buf);
}

/* ---------- scratch directory and running commands ---------- */

static char scratch_root[PATH_MAX];   /* mkdtemp result */
static char work_dir[PATH_MAX];       /* scratch_root/work: where commands run */
static char out_file[PATH_MAX];       /* scratch_root/out: captured output */

/*
 * Runs `script` with zsh in `dir`. Output goes to `capture` (or is discarded
 * when NULL). Returns the exit status, or -1 if it had to be killed.
 */
static int run_shell(const char *script, const char *dir, const char *capture, const char *cmd_text) {
    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (pid == 0) {
        if (chdir(dir) != 0) _exit(126);
        int fd = open(capture ? capture : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) _exit(126);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
        int in = open("/dev/null", O_RDONLY);
        if (in >= 0) { dup2(in, STDIN_FILENO); close(in); }
        setenv("OUT", out_file, 1);
        setenv("CMD", cmd_text ? cmd_text : "", 1);
        signal(SIGINT, SIG_DFL);   /* the tutor ignores Ctrl-C; the command must not */
        alarm(COMMAND_TIMEOUT_SEC);
        execl(SHELL, "zsh", "-f", "-c", script, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(status)) return -1;
    return WEXITSTATUS(status);
}

static void reset_work_dir(void) {
    char script[PATH_MAX + 64];
    snprintf(script, sizeof script, "rm -rf work && mkdir work && cd work && :");
    run_shell(script, scratch_root, NULL, NULL);
    run_shell(FIXTURES, work_dir, NULL, NULL);
}

static void setup_scratch(void) {
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') n--;
    snprintf(scratch_root, sizeof scratch_root, "%.*s/shell-tutor.XXXXXX", (int)n, tmp);
    if (!mkdtemp(scratch_root)) die("mkdtemp");
    snprintf(work_dir, sizeof work_dir, "%s/work", scratch_root);
    snprintf(out_file, sizeof out_file, "%s/out", scratch_root);
}

static void remove_scratch(void) {
    if (!*scratch_root) return;
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/rm", "rm", "-rf", scratch_root, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

static void show_output(int status) {
    FILE *f = fopen(out_file, "r");
    if (!f) return;
    char line[4096];
    int lines = 0;
    while (fgets(line, sizeof line, f)) {
        if (++lines > MAX_OUTPUT_LINES) {
            printf("%s  ... (output cut after %d lines)%s\n", DIM, MAX_OUTPUT_LINES, RESET);
            break;
        }
        printf("%s  %s%s%s", DIM, line, line[strlen(line) - 1] == '\n' ? "" : "\n", RESET);
    }
    fclose(f);
    if (status == -1) printf("%s  (stopped after %d seconds)%s\n", RED, COMMAND_TIMEOUT_SEC, RESET);
    else if (status != 0) printf("%s  (exit status %d)%s\n", DIM, status, RESET);
}

/* ---------- progress ---------- */

static char progress_path[PATH_MAX];
static int done[LESSON_COUNT];

static void load_progress(void) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(progress_path, sizeof progress_path, "%s/.shell-tutor/progress", home);
    FILE *f = fopen(progress_path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *id = trim(line);
        for (int i = 0; i < LESSON_COUNT; i++)
            if (strcmp(LESSONS[i].id, id) == 0) done[i] = 1;
    }
    fclose(f);
}

static void save_progress(void) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", progress_path);
    *strrchr(dir, '/') = '\0';
    mkdir(dir, 0700);
    FILE *f = fopen(progress_path, "w");
    if (!f) return;
    for (int i = 0; i < LESSON_COUNT; i++)
        if (done[i]) fprintf(f, "%s\n", LESSONS[i].id);
    fclose(f);
}

static void reset_progress(void) {
    memset(done, 0, sizeof done);
    unlink(progress_path);
}

/* ---------- lessons ---------- */

static void print_header(int i, int review) {
    const Lesson *l = &LESSONS[i];
    if (review) {
        printf("\n%s%sReview  %s — %s%s\n\n", BOLD, CYAN, l->section, l->title, RESET);
        return;
    }
    printf("\n%s%s%d/%d  %s — %s%s\n\n", BOLD, CYAN, i + 1, LESSON_COUNT, l->section, l->title, RESET);
    printf("%s\n\n", l->explain);
}

static void print_task(const Lesson *l) {
    printf("%s%sTask:%s %s\n", BOLD, YELLOW, RESET, l->task);
}

static void print_prompt_help(int kind) {
    if (kind == RUN)
        printf("%sType a command, or: hint, idk (show the answer), skip, list, quit%s\n", DIM, RESET);
    else
        printf("%sType a letter, or: hint, idk (show the answer), skip, list, quit%s\n", DIM, RESET);
}

/* "I don't know" in its usual spellings, plus the older `answer`. */
static int wants_answer(const char *input) {
    static const char *forms[] = { "idk", "i don't know", "i dont know", "dunno", "answer", "show", NULL };
    for (int k = 0; forms[k]; k++)
        if (strcasecmp(input, forms[k]) == 0) return 1;
    return 0;
}

static void list_lessons(int current) {
    putchar('\n');
    const char *section = "";
    for (int i = 0; i < LESSON_COUNT; i++) {
        if (strcmp(section, LESSONS[i].section) != 0) {
            section = LESSONS[i].section;
            printf("%s%s%s\n", BOLD, section, RESET);
        }
        printf("  %s%2d%s %s %s%s\n",
               i == current ? BOLD : "", i + 1, RESET,
               done[i] ? "✓" : " ", LESSONS[i].title, i == current ? "  ← you are here" : "");
    }
    putchar('\n');
}

enum { NEXT, QUIT, JUMP };
static int jump_to;

/* Handles the commands shared by both lesson kinds. Returns 1 if it consumed the input. */
static int meta_command(const char *input, const Lesson *l, int i, int *result) {
    if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) { *result = QUIT; return 1; }
    if (strcmp(input, "skip") == 0 || strcmp(input, "next") == 0) { *result = NEXT; return 1; }
    if (strcmp(input, "hint") == 0) { printf("%sHint:%s %s\n", YELLOW, RESET, l->hint); return 2; }
    if (strcmp(input, "list") == 0) { list_lessons(i); return 2; }
    if (strcmp(input, "help") == 0) { print_prompt_help(l->kind); return 2; }
    if (strncmp(input, "goto ", 5) == 0) {
        int n = atoi(input + 5);
        if (n >= 1 && n <= LESSON_COUNT) { jump_to = n - 1; *result = JUMP; return 1; }
        printf("Lessons are numbered 1 to %d.\n", LESSON_COUNT);
        return 2;
    }
    return 0;
}

/* A failed review sends the lesson back into the pool so it is taught again. */
static void forget(int i) {
    done[i] = 0;
    save_progress();
}

static int run_lesson(int i, int review) {
    const Lesson *l = &LESSONS[i];
    print_header(i, review);
    reset_work_dir();
    print_task(l);
    print_prompt_help(RUN);
    char buf[2048];
    int saw_answer = 0;   /* a pass after seeing the answer doesn't count; the lesson returns later */
    int tries = 0;
    for (;;) {
        char *input = read_line("$ ", buf, sizeof buf);
        if (!input) return QUIT;
        if (!*input) continue;
        int result, handled = meta_command(input, l, i, &result);
        if (handled == 1) return result;
        if (handled == 2) continue;
        if (wants_answer(input)) {
            saw_answer = 1;
            printf("%sOne way:%s  %s\n", GREEN, RESET, l->answer);
            if (review) { printf("Back into the lessons it goes.\n"); forget(i); return NEXT; }
            printf("Type it yourself to move on (the scratch files are reset), or skip. Either way this one comes around again later.\n");
            reset_work_dir();
            continue;
        }
        int status = run_shell(input, work_dir, out_file, input);
        show_output(status);
        if (run_shell(l->check, work_dir, NULL, input) == 0) {
            if (saw_answer) {
                printf("%s✓ That's it.%s You'll get this one again later, without the answer.\n", GREEN, RESET);
                return NEXT;
            }
            printf("%s✓ Correct.%s\n", GREEN, RESET);
            done[i] = 1;
            save_progress();
            return NEXT;
        }
        if (review && ++tries >= REVIEW_TRIES) {
            printf("%sNot quite.%s One way:  %s\nBack into the lessons it goes.\n", RED, RESET, l->answer);
            forget(i);
            return NEXT;
        }
        printf("%sNot quite.%s Try again, or type hint.\n", RED, RESET);
        reset_work_dir();
    }
}

static int quiz_lesson(int i, int review) {
    const Lesson *l = &LESSONS[i];
    print_header(i, review);
    print_task(l);
    printf("%s\n", l->options);
    print_prompt_help(QUIZ);
    char buf[256];
    int tries = 0;
    for (;;) {
        char *input = read_line("> ", buf, sizeof buf);
        if (!input) return QUIT;
        if (!*input) continue;
        int result, handled = meta_command(input, l, i, &result);
        if (handled == 1) return result;
        if (handled == 2) continue;
        if (wants_answer(input)) {
            printf("%sAnswer: %s.%s %s\n", GREEN, l->check, RESET, l->answer);
            printf("This one comes around again later.\n");
            if (review) forget(i);
            return NEXT;
        }
        if (strlen(input) == 1 && tolower((unsigned char)input[0]) == l->check[0]) {
            printf("%s✓ Correct.%s %s\n", GREEN, RESET, l->answer);
            done[i] = 1;
            save_progress();
            return NEXT;
        }
        if (strlen(input) == 1 && input[0] >= 'a' && input[0] <= 'd') {
            if (review && ++tries >= REVIEW_TRIES) {
                printf("%sNot quite.%s Answer: %s. %s\nBack into the lessons it goes.\n", RED, RESET, l->check, l->answer);
                forget(i);
                return NEXT;
            }
            printf("%sNot quite.%s Try again, or type hint.\n", RED, RESET);
            continue;
        }
        printf("Answer with a letter (a-d).\n");
    }
}

/* Re-asks the lessons in pool[0..n) in random order, `count` of them at most. */
static int review_lessons(int *pool, int n, int count) {
    for (int r = 0; r < count; r++) {
        int pick = r + rand() % (n - r);     /* partial shuffle */
        int tmp = pool[r]; pool[r] = pool[pick]; pool[pick] = tmp;
        int k = pool[r];
        int result = LESSONS[k].kind == RUN ? run_lesson(k, 1) : quiz_lesson(k, 1);
        if (result == QUIT) return QUIT;
        if (result == JUMP) return JUMP;
    }
    return NEXT;
}

/*
 * At the end of a section, re-ask a few finished lessons from the sections
 * just before it (not from the very beginning every time). No explanation is
 * shown; two misses (or idk) un-finish the lesson so it is taught again later.
 */
static int review_pass(int section_end) {
    int pool[LESSON_COUNT], n = 0;
    const char *section = LESSONS[section_end].section;
    const char *seen[REVIEW_DEPTH + 1];
    int depth = 0;
    for (int k = section_end - 1; k >= 0; k--) {
        const char *sec = LESSONS[k].section;
        if (strcmp(sec, section) == 0) continue;
        int known = 0;
        for (int d = 0; d < depth; d++) if (strcmp(seen[d], sec) == 0) known = 1;
        if (!known) {
            if (depth == REVIEW_DEPTH) break;
            seen[depth++] = sec;
        }
        if (done[k]) pool[n++] = k;
    }
    if (n == 0) return NEXT;
    int count = n < REVIEW_SIZE ? n : REVIEW_SIZE;
    printf("\n%s%sQuick review%s — %d from the last few sections, no explanations this time.\n", BOLD, YELLOW, RESET, count);
    return review_lessons(pool, n, count);
}

/* Once everything is finished: every lesson once more, in random order. */
static int final_review(void) {
    int pool[LESSON_COUNT];
    for (int k = 0; k < LESSON_COUNT; k++) pool[k] = k;
    printf("\n%s%sFinal review%s — all %d lessons, mixed up, no explanations. Anything you miss gets taught again.\n",
           BOLD, YELLOW, RESET, LESSON_COUNT);
    return review_lessons(pool, LESSON_COUNT, LESSON_COUNT);
}

/* ---------- main ---------- */

static void usage(void) {
    puts("shell-tutor - learn the Unix shell by doing\n"
         "\n"
         "  shell-tutor          continue from the first unfinished lesson\n"
         "  shell-tutor N        start at lesson N\n"
         "  shell-tutor --list   show the lessons and your progress\n"
         "  shell-tutor --reset  forget your progress\n"
         "\n"
         "At the prompt: hint, idk (show the answer), skip, list, goto N, quit.");
}

int main(int argc, char **argv) {
    use_color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    load_progress();

    int start = -1;
    if (argc > 1) {
        if (strcmp(argv[1], "--list") == 0) { list_lessons(-1); return 0; }
        if (strcmp(argv[1], "--reset") == 0) { reset_progress(); puts("Progress cleared."); return 0; }
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) { usage(); return 0; }
        int n = atoi(argv[1]);
        if (n < 1 || n > LESSON_COUNT) { usage(); return 2; }
        start = n - 1;
    }
    if (start < 0) {
        for (int i = 0; i < LESSON_COUNT; i++) if (!done[i]) { start = i; break; }
        if (start < 0) start = LESSON_COUNT;   /* everything done: straight to the final review */
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    setup_scratch();
    atexit(remove_scratch);
    signal(SIGINT, SIG_IGN);   /* Ctrl-C at the tutor prompt shouldn't kill the tutor; it still stops a running command */

    printf("%s%sshell-tutor%s — commands run in a scratch folder (%s), never in your files.\n", BOLD, CYAN, RESET, work_dir);
    int any_done = 0;
    for (int k = 0; k < LESSON_COUNT; k++) any_done += done[k];
    if (!any_done) {
        printf("\nNew here? The shell is the program behind this window: you type a command,\n"
               "press Enter, and it runs it and shows the result. A command is a program's name,\n"
               "sometimes followed by options (like -l) and the files it should work on.\n"
               "Each lesson explains one thing, then asks you to try it. If you have no idea,\n"
               "type   idk   to see an answer, then type that yourself to see what it does.\n"
               "Nothing you try here can damage anything.\n");
    }

    int i = start, quit = 0;
    while (i < LESSON_COUNT) {
        int result = LESSONS[i].kind == RUN ? run_lesson(i, 0) : quiz_lesson(i, 0);
        if (result == QUIT) { quit = 1; break; }
        if (result == JUMP) { i = jump_to; continue; }
        int section_over = i + 1 == LESSON_COUNT || strcmp(LESSONS[i].section, LESSONS[i + 1].section) != 0;
        i++;
        if (section_over && done[i - 1]) {
            result = review_pass(i - 1);
            if (result == QUIT) { quit = 1; break; }
            if (result == JUMP) { i = jump_to; continue; }
        }
    }

    /* Lessons that were skipped (or answered with idk) come around again
       until they're done or the user gives up on them a second time. */
    int reviewed_all = 0;
    while (!quit) {
        int pending = 0, done_this_pass = 0;
        for (int k = 0; k < LESSON_COUNT; k++) pending += !done[k];
        if (pending == 0 && !reviewed_all) {
            reviewed_all = 1;
            int result = final_review();
            if (result == QUIT) { quit = 1; break; }
            continue;   /* anything forgotten during the review is taught again below */
        }
        if (pending == 0) {
            printf("\n%sThat's all %d lessons, reviewed and all. Well done.%s\n", GREEN, LESSON_COUNT, RESET);
            printf("Run   shell-tutor   again any time for another full review, or   shell-tutor --reset   to start from scratch.\n");
            break;
        }
        printf("\n%sGoing back to the %d lesson%s you skipped or needed the answer for.%s\n", YELLOW, pending, pending == 1 ? "" : "s", RESET);
        for (int k = 0; k < LESSON_COUNT && !quit; k++) {
            if (done[k]) continue;
            int result = LESSONS[k].kind == RUN ? run_lesson(k, 0) : quiz_lesson(k, 0);
            if (result == QUIT) quit = 1;
            if (result == JUMP) k = jump_to - 1;
            if (done[k]) done_this_pass++;
        }
        if (quit) break;
        if (!done_this_pass) {
            printf("\n%sLeaving the rest for another time. Run shell-tutor again to pick them up.%s\n", DIM, RESET);
            break;
        }
    }

    int finished = 0;
    for (int k = 0; k < LESSON_COUNT; k++) finished += done[k];
    printf("%s%d of %d lessons done. Run shell-tutor again to continue.%s\n", DIM, finished, LESSON_COUNT, RESET);
    return 0;
}
