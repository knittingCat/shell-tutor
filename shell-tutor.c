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
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#define SHELL "/bin/zsh"
#define COMMAND_TIMEOUT_SEC 15
#define MAX_OUTPUT_LINES 40
#define REVIEW_SIZE 3         /* earlier lessons re-asked at the end of each section */
#define REVIEW_POOL 10        /* ...drawn from the nearest earlier sections holding this many done lessons */
#define REVIEW_TRIES 2
#define REVIEW_TRIES_ADVANCED 4   /* advanced answers are often two steps: write the script, run it */

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
    const char *diagnose; /* RUN, optional: zsh snippet run after a failed check; whatever it prints is shown */
} Lesson;

/*
 * The check snippet runs in the scratch directory after the user's command,
 * with these variables set:
 *   $OUT  path to a file holding everything the command printed
 *   $CMD  the command text the user typed
 */
static const Lesson BASIC[] = {
    { "pwd", RUN, "Getting around", "Where am I?",
      "Directory is the shell's word for a folder; the two mean exactly the same\n"
      "thing, and you'll see both. The shell always has a current directory: the\n"
      "folder your commands act on. There's a three-letter command that prints its\n"
      "full path; its name is short for \"print working directory\".",
      "Find out which folder you are in.",
      "grep -qx \"$(pwd)\" \"$OUT\" && [[ \"$CMD\" == *pwd* ]]",
      "p, then w, then d.",
      "pwd", NULL, NULL },

    { "ls", RUN, "Getting around", "What's here?",
      "There's a two-letter command, short for \"list\", that shows the files and\n"
      "folders in the current directory. Give it a folder name afterwards and it\n"
      "lists that folder instead.",
      "See what is in the current directory. (One of the things in it is a folder called docs.)",
      "grep -q 'fruits.txt' \"$OUT\" && grep -q 'docs' \"$OUT\"",
      "l, then s.",
      "ls", NULL, NULL },

    { "ls-la", RUN, "Getting around", "Hidden files and details",
      "Most commands accept options: extra words that start with a dash and change\n"
      "what the command does. Each option does one thing only. ls has two you'll\n"
      "use constantly:\n"
      "  ls -a lists ALL files: the ones plain ls shows, plus the hidden ones.\n"
      "        A file whose name starts with a dot, like .secret, is hidden:\n"
      "        plain ls leaves it out, ls -a includes it. One command, one list.\n"
      "  ls -l changes the format, not the selection: a long listing, one file\n"
      "        per line, with its permissions, owner, size and date. It shows\n"
      "        the same files as plain ls, so hidden files are still left out.\n"
      "To get both effects you give both options to one ls. Several one-letter\n"
      "options can share a single dash: ls -l -t and ls -lt mean the same thing.",
      "Show the hidden file too, in the long format.",
      "grep -q '\\.secret' \"$OUT\" && grep -qE '^[-d][rwx-]{9}' \"$OUT\"",
      "-l alone gives the format but not the hidden file; -a alone gives the hidden file but not the format. Give ls both.",
      "ls -la", NULL,
      "[[ \"$CMD\" == *';'* || \"$CMD\" == *'&&'* ]] && echo 'Two commands give two lists. One ls with both options gives one list that has everything.'; "
      "grep -q '\\.secret' \"$OUT\" || echo 'The hidden file .secret is not in your list: that needs -a.'; "
      "grep -qE '^[-d][rwx-]{9}' \"$OUT\" || echo 'That is the short format: the long one needs -l.'" },

    { "echo", RUN, "Getting around", "Printing text",
      "  echo prints whatever words follow it, then a new line:\n"
      "  echo hi there prints hi there\n"
      "It sounds pointless, but it's the shell's way of saying something: for\n"
      "messages in scripts, for checking what a variable holds, and for putting\n"
      "text into files, all of which come later.",
      "Print: good morning",
      "[ \"$(cat \"$OUT\")\" = \"good morning\" ]",
      "echo, then the words.",
      "echo good morning", NULL,
      "grep -qi 'command not found' \"$OUT\" && echo 'The shell looked for a program with that name. The printing command is echo; the words come after it.'" },

    { "semicolon", RUN, "Getting around", "Two commands using 1 line",
      "Normally you type one command, press Enter, and it runs. To run two in a row\n"
      "from a single line, separate them with a semicolon:\n"
      "  echo one ; echo two\n"
      "runs echo one, and when that has finished, echo two. Spaces around the ;\n"
      "are optional. Any commands can be chained this way, as many as you like.",
      "Using 1 line, print the word hello and then the word bye.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'hello\\nbye')\" ] && [[ \"$CMD\" == *';'* ]]",
      "echo hello ; echo bye",
      "echo hello ; echo bye", NULL,
      "[[ \"$CMD\" == *';'* ]] || echo 'Put a ; between the two commands so both run from this one line.'; "
      "grep -q '^hello bye$' \"$OUT\" && echo 'That is one echo printing two words. Two separate commands, one per word.'" },

    { "paths", RUN, "Getting around", "Paths and the slash",
      "A path is the address of a file or folder. The slash / does two jobs:\n"
      "\n"
      "  Between names it separates a folder from what's inside it: docs/guide\n"
      "  means \"the guide folder inside the docs folder\", and docs/readme.md\n"
      "  means \"the file readme.md inside docs\". Any command that takes a file or\n"
      "  folder name takes a path like this instead: ls docs/guide\n"
      "\n"
      "  At the very start it means the root: the top of the whole disk, the one\n"
      "  folder that contains everything else. /Users/ann/Desktop starts at the\n"
      "  root and walks down, so it names the same place no matter where you are.\n"
      "  That's an absolute path.\n"
      "\n"
      "A path that does NOT start with / is relative: it starts from the folder you\n"
      "are in right now (the one pwd prints). Three shortcuts you'll see everywhere:\n"
      "  . the folder you are in .. the folder above it\n"
      "  ~ your home folder (where your Desktop and Documents live)\n"
      "So ~/docs would be a docs folder in your home, not the one here.\n"
      "\n"
      "Putting it to use. You know from lesson 2 that ls followed by a folder name\n"
      "lists that folder: ls docs shows what is in docs. A path can go in the same\n"
      "place, so ls docs/guide would list the guide folder that is inside docs. This\n"
      "scratch folder has that: docs contains a folder called guide, and guide\n"
      "contains one file. Note that ls guide would fail: there is no guide in the\n"
      "folder you are in, only inside docs, so the path has to go through docs.",
      "List what is inside the guide folder, using a relative path from here.",
      "grep -q 'setup.md' \"$OUT\" && [[ \"$CMD\" == *docs/guide* ]]",
      "ls, then the path: docs, slash, guide.",
      "ls docs/guide", NULL,
      "[[ \"$CMD\" == *'~'* ]] && echo '~ is your home folder; the docs folder is here, in the current folder, so no ~.'; "
      "[[ \"$CMD\" =~ '(^|[[:space:]])/docs' ]] && echo 'A path starting with / begins at the root of the disk. This docs folder is inside the current folder, so the path starts with docs, no leading slash.'; "
      "[[ \"$CMD\" =~ '(^|[[:space:]])guide' ]] && echo 'guide is not in this folder; it is inside docs. The path goes through docs first: docs/guide'; "
      "grep -q 'readme.md' \"$OUT\" && [[ \"$CMD\" != *docs/guide* ]] && echo 'That is the docs folder itself. guide is one level further down: docs/guide'" },

    { "cd", RUN, "Getting around", "Going into a folder",
      "So far you have looked into folders from outside. cd (change directory)\n"
      "takes you inside one: it makes that folder your current directory, so from\n"
      "then on relative paths start there. Nothing on disk moves, only you.\n"
      "  cd FOLDER goes into FOLDER (any path works: relative like docs, or\n"
      "            absolute like /Users/ann).\n"
      "  cd .. goes up one level. cd on its own goes to your home folder, which\n"
      "  the shell also calls ~ (so cd ~ is the same, and ~/Desktop is the Desktop\n"
      "  inside it).\n"
      "Each line you type here runs in a fresh shell that is thrown away afterwards,\n"
      "so a cd on its own would be forgotten immediately. Use the ; from the lesson\n"
      "\"Two commands using 1 line\": cd into the folder, then a ; and then, on the same\n"
      "line, the command from lesson 1 that prints where you are.",
      "Go into the docs folder and show the directory you are then in.",
      "grep -qx \"$(pwd)/docs\" \"$OUT\"",
      "cd docs ; pwd",
      "cd docs; pwd", NULL,
      "[[ \"$CMD\" == *'~'* ]] && echo '~ means your home folder, and the docs folder is not there: it is inside the folder you are in now, so its path is just docs'; "
      "grep -qi 'no such file' \"$OUT\" && [[ \"$CMD\" != *'~'* ]] && echo 'That folder was not found: relative to here it is called docs, with no slash in front.'; "
      "grep -qi 'not a directory' \"$OUT\" && echo 'cd can only go into a folder, and that path names a file. Stop at the folder: cd docs'; "
      "[[ \"$CMD\" == *cd* ]] || echo 'You never changed directory: start with cd docs.'; "
      "[[ \"$CMD\" == *cd* && \"$CMD\" != *pwd* ]] && echo 'Nothing shows where you ended up: add ; pwd on the same line after the cd.'" },

    { "cat", RUN, "Reading files", "Show a file",
      "  cat FILE prints a file's contents. Short for \"concatenate\": given\n"
      "several files it prints them one after another.",
      "Print the contents of fruits.txt.",
      "grep -q '^banana$' \"$OUT\" && grep -q '^cherry$' \"$OUT\"",
      "cat, then the file name.",
      "cat fruits.txt", NULL,
      "grep -q banana \"$OUT\" || echo 'The contents of fruits.txt did not appear.'" },

    { "head-tail", RUN, "Reading files", "Just the start or the end",
      "Long files are easier to peek at:\n"
      "  head -n 3 FILE first 3 lines tail -n 3 FILE last 3 lines\n"
      "numbers.txt holds the numbers 1 to 20, one per line.",
      "Print only the last 2 lines of numbers.txt.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf '19\\n20')\" ]",
      "tail with -n 2.",
      "tail -n 2 numbers.txt", NULL,
      "[[ \"$CMD\" == *head* ]] && echo 'head gives the START of the file; the last lines come from tail.'; n=$(wc -l < \"$OUT\"); [ \"$n\" -eq 2 ] || echo \"That printed $n lines, not 2: use -n 2.\"" },

    { "tail-f", QUIZ, "Reading files", "Watching a file grow",
      "  tail -f FILE prints the last lines of FILE and then keeps waiting: every\n"
      "time something is added to the file, the new lines appear at once. It is\n"
      "the standard way to watch a log while a program writes to it.\n"
      "\n"
      "Unlike every command so far, it never finishes on its own, so the prompt\n"
      "doesn't come back. To stop a command that is still running, hold the Control\n"
      "key and press C (written Ctrl-C). The command is ended and you get your\n"
      "prompt back. This works on any command that is taking too long or that you\n"
      "started by mistake; you'll use it a lot.",
      "You ran tail -f app.log and have seen enough. How do you get your prompt back?",
      "b",
      "The same key that stops any running command.",
      "Ctrl-C ends the running command and gives you the prompt back. Typing q or\n"
      "exit does nothing here: tail isn't reading what you type, it's reading the\n"
      "file. And a log may keep growing for days.",
      "a) press q\nb) press Ctrl-C\nc) wait for the file to stop growing\nd) type exit", NULL },

    { "mkdir", RUN, "Making and changing files", "Make a folder",
      "  mkdir NAME creates a directory.\n"
      "  mkdir -p a/b/c creates the whole chain at once.",
      "Create a folder called photos.",
      "[ -d photos ]",
      "mkdir, then the name.",
      "mkdir photos", NULL,
      "[ -e photos ] || echo 'There is no photos folder yet.'; [ -f photos ] && echo 'photos exists but it is a file, not a folder: mkdir makes folders.'" },

    { "cp", RUN, "Making and changing files", "Copy a file",
      "  cp SOURCE DEST copies a file. DEST can be a new file name or a folder.\n"
      "  cp -r FOLDER DEST copies a whole folder.",
      "Make a copy of fruits.txt called fruits-backup.txt.",
      "[ -f fruits-backup.txt ] && cmp -s fruits.txt fruits-backup.txt",
      "cp fruits.txt <new name>",
      "cp fruits.txt fruits-backup.txt", NULL,
      "[ -e fruits-backup.txt ] || echo 'No file called fruits-backup.txt exists yet.'; [ -f fruits.txt ] || echo 'fruits.txt is gone: that was a move, not a copy. Use cp.'" },

    { "mv", RUN, "Making and changing files", "Rename or move",
      "  mv OLD NEW renames a file, or moves it if NEW is a folder.\n"
      "There is no separate rename command: a rename is a move.",
      "Rename notes.txt to todo.txt.",
      "[ -f todo.txt ] && [ ! -e notes.txt ]",
      "mv, old name, new name.",
      "mv notes.txt todo.txt", NULL,
      "[ -e notes.txt ] && echo 'notes.txt is still here: mv should leave only todo.txt.'; [ -e todo.txt ] || echo 'There is no todo.txt yet.'" },

    { "rm", RUN, "Making and changing files", "Delete",
      "  rm FILE deletes a file. There is no trash and no undo.\n"
      "  rm -r FOLDER deletes a folder and everything inside it.\n"
      "Be careful with rm: it does exactly what you say, immediately.",
      "Delete the file called old.log.",
      "[ ! -e old.log ] && [ -f fruits.txt ]",
      "rm, then the file name.",
      "rm old.log", NULL,
      "[ -e old.log ] && echo 'old.log is still here.'; [ -f fruits.txt ] || echo 'fruits.txt was deleted too: rm only old.log.'" },

    { "redirect", RUN, "Redirection", "Send output to a file",
      "Every command's output normally goes to the screen. Put > and a file name\n"
      "after a command and its output goes into that file instead. Nothing appears\n"
      "on screen; the file is created, or emptied and refilled if it already exists.\n"
      "  ls > listing.txt puts the listing into listing.txt.\n"
      "  echo some words > note.txt creates note.txt containing: some words\n"
      "That second form is the usual way to make a small file: echo would print\n"
      "the words, and > catches them into the file. The shape is always\n"
      "  COMMAND > FILE",
      "Create a file called greeting.txt containing the single word hello.",
      "[ \"$(cat greeting.txt 2>/dev/null)\" = \"hello\" ]",
      "echo hello > greeting.txt",
      "echo hello > greeting.txt", NULL,
      "[ -e greeting.txt ] || echo 'No greeting.txt was created: send the output into it with >.'; [ -e greeting.txt ] && [ \"$(cat greeting.txt)\" != hello ] && echo \"greeting.txt contains '$(cat greeting.txt)', not hello.\"" },

    { "append", RUN, "Redirection", "Add to the end of a file",
      "> throws away whatever the file held before. To keep it and add more at the\n"
      "end, use two of them: >> appends. Each command adds its output as new lines\n"
      "after the existing ones:\n"
      "  echo first > note.txt    note.txt now holds one line: first\n"
      "  echo second >> note.txt  note.txt now holds two lines: first, second\n"
      "  echo third > note.txt    back to one line: third (the > replaced it all)\n"
      "fruits.txt here holds three lines: apple, banana, cherry.",
      "Add a fourth line, the word mango, to the end of fruits.txt, keeping the three lines already there.",
      "[ \"$(tail -n 1 fruits.txt)\" = \"mango\" ] && grep -q '^apple$' fruits.txt && [ \"$(wc -l < fruits.txt | tr -d ' ')\" = 4 ]",
      "echo the word, then >> and the file name.",
      "echo mango >> fruits.txt", NULL,
      "grep -q '^apple$' fruits.txt || echo 'The original lines are gone: > replaced the file. Appending is >>.'; "
      "grep -q '^mango$' fruits.txt || echo 'mango was not added to fruits.txt.'; "
      "[ \"$(grep -c '^mango$' fruits.txt)\" -gt 1 ] && echo 'mango was added more than once; the file should end with exactly one.'" },

    { "pipe", RUN, "Pipes", "Connect two commands",
      "First a new command. grep reads lines and prints only the ones that\n"
      "contain a given piece of text: grep an fruits.txt prints the lines of\n"
      "fruits.txt containing \"an\". The word after grep is what to look for.\n"
      "(The name is short for \"global regular expression print\": search\n"
      "everywhere for a pattern and print the matches.)\n"
      "Now the pipe. The | sign (a pipe) sends one command's output into the next\n"
      "command's input, so given no file name, grep reads whatever is piped in:\n"
      "  cat fruits.txt | grep an does the same as above, in two steps.\n"
      "That is how small tools get combined into bigger ones.",
      "Show only the lines of fruits.txt that contain the letter e.",
      "[[ \"$CMD\" == *'|'* ]] && grep -q '^cherry$' \"$OUT\" && ! grep -q '^banana$' \"$OUT\"",
      "cat the file, pipe it into grep e.",
      "cat fruits.txt | grep e", NULL,
      "[[ \"$CMD\" == *'|'* ]] || echo 'No pipe in that command: send the output of one command into grep with |.'; grep -q '^banana$' \"$OUT\" && echo 'banana came through, and it has no e: grep should keep only matching lines.'" },

    { "wc", RUN, "Pipes", "Count things",
      "wc is short for \"word count\", though it counts more than words:\n"
      "  wc -l counts lines (that is the letter l, as in lines, not the digit\n"
      "one). wc -w counts words. wc -c counts bytes.\n"
      "With no option it prints all three. Given a file name it prints the\n"
      "count and the name; fed through a pipe it prints just the number.",
      "Print how many lines numbers.txt has, using a pipe so only the number appears.",
      "[[ \"$CMD\" == *'|'* ]] && [ \"$(tr -d ' ' < \"$OUT\")\" = \"20\" ]",
      "cat numbers.txt | wc -l",
      "cat numbers.txt | wc -l", NULL,
      "[[ \"$CMD\" == *'-1'* ]] && echo 'That is the digit one. The option is the letter l, as in lines: wc -l.'; [[ \"$CMD\" == *'|'* ]] || echo 'Use a pipe: cat the file and pipe it into wc -l.'; grep -q 'numbers.txt' \"$OUT\" && echo 'The file name is in the output: that means wc was given the file name instead of piped input.'" },

    { "sort-uniq", RUN, "Pipes", "Sort and de-duplicate",
      "  sort puts lines in order. uniq drops repeated lines, but only when\n"
      "they are next to each other, so on its own it misses repeats that are apart.\n"
      "colors.txt has repeated colors in a random order.",
      "Print each color in colors.txt once, in alphabetical order.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'blue\\ngreen\\nred\\nyellow')\" ]",
      "sort colors.txt | uniq",
      "sort colors.txt | uniq", NULL,
      "[[ \"$CMD\" == *sort* ]] || echo 'The lines are not sorted: uniq only removes repeats that are next to each other, so sort first.'; [[ \"$CMD\" == *uniq* ]] || echo 'Repeats are still there: pipe the sorted lines into uniq.'; [[ \"$CMD\" == *uniq*sort* ]] && echo 'uniq ran before sort, so the repeats were still apart when uniq looked at them. Sort first, then uniq.'" },

    { "find", RUN, "Searching", "Find files by name",
      "  find . -name 'PATTERN' searches the current directory (.) and every\n"
      "folder inside it for names matching PATTERN. In a pattern, * stands for\n"
      "\"any characters\", so '*.txt' means \"anything ending in .txt\". It only\n"
      "stands in where you put it: '*fruits' is names ending in fruits (not\n"
      "fruits.txt), 'fruits*' names starting with it, '*fruits*' names containing\n"
      "it anywhere.\n"
      "The quotes matter: the shell itself also knows *, and before running a\n"
      "command it swaps an unquoted *.txt for the matching names in the current\n"
      "folder only. Quoted, the pattern reaches find untouched, and find does the\n"
      "matching in every folder.\n"
      "-name is one test of several. Plain find . lists everything below you.\n"
      "  -iname 'PATTERN' matches ignoring case (README.MD too)\n"
      "  -type d only directories, -type f only files\n"
      "  -mtime -1 changed in the last day\n"
      "  -size +1M bigger than 1 megabyte\n"
      "Tests combine: find . -name '*.txt' -mtime -1 is .txt files changed today.\n"
      "The tests can go in any order, but each one takes the word right after it\n"
      "as its value, so keep -name and its pattern together.",
      "Find every file whose name ends in .md, anywhere under the current directory.",
      "grep -q 'docs/readme.md' \"$OUT\" && grep -q 'docs/guide/setup.md' \"$OUT\" && ! grep -q 'fruits.txt' \"$OUT\"",
      "find . -name '*.md'",
      "find . -name '*.md'", NULL,
      "grep -q 'setup.md' \"$OUT\" || echo 'docs/guide/setup.md was not found: find searches every folder below the one you give it (start from .).'; grep -q 'fruits.txt' \"$OUT\" && echo 'fruits.txt matched too: the -name pattern should only match .md.'" },

    { "grep-r", RUN, "Searching", "Search inside files",
      "You met grep in the Pipes section, picking lines out of one file. Given a\n"
      "folder instead of a file it refuses, unless you add -r (recursive): then it\n"
      "looks inside every file under that folder, and prints each matching line\n"
      "with the name of the file it came from in front.\n"
      "  grep -r WORD FOLDER\n"
      "Two more options: -i ignores case, so it matches Install and INSTALL too.\n"
      "-n adds the line number. Options can be run together: -ri, -rn, -rin.",
      "Find which file under docs mentions the word install (any case).",
      "grep -q 'setup.md' \"$OUT\" && ! grep -q 'readme.md' \"$OUT\"",
      "grep -ri install docs",
      "grep -ri install docs", NULL,
      "grep -q 'setup.md' \"$OUT\" || echo 'setup.md was not reported: it says INSTALL in capitals, so ignore case with -i, and search the docs folder recursively with -r.'" },

    { "chmod", RUN, "Scripts", "Make a script runnable",
      "hello.sh is a shell script, but it can't be run yet: files need the\n"
      "execute permission first.\n"
      "  chmod +x FILE adds it. A script in the current directory is then run as\n"
      "  ./NAME (the ./ means \"the one in this directory\", since the shell only\n"
      "searches PATH otherwise).",
      "Make hello.sh executable and run it.",
      "[ -x hello.sh ] && grep -q 'Hello from a script' \"$OUT\"",
      "chmod +x hello.sh ; ./hello.sh",
      "chmod +x hello.sh; ./hello.sh", NULL,
      "[ -x hello.sh ] || echo 'hello.sh is still not executable: chmod +x hello.sh first.'; grep -q 'Hello from a script' \"$OUT\" || echo 'The script did not run: after chmod, run it as ./hello.sh on the same line.'" },

    { "vars", RUN, "Scripts", "Variables",
      "  NAME=value sets a shell variable (no spaces around the =).\n"
      "  $NAME uses it. Double quotes keep spaces together: \"$NAME\".\n"
      "  $(command) is replaced by the command's output.",
      "Set a variable to the output of whoami and echo I am <that name>.",
      "grep -q \"^I am $(whoami)$\" \"$OUT\" && [[ \"$CMD\" == *'$'* ]]",
      "me=$(whoami); echo \"I am $me\"",
      "me=$(whoami); echo \"I am $me\"", NULL,
      "[[ \"$CMD\" == *'$'* ]] || echo 'No variable was used: set one with name=$(whoami) and use it as $name.'; grep -q \"I am $(whoami)\" \"$OUT\" || echo 'The output should read exactly: I am <your user name>.'" },

    { "and-or", RUN, "Combining commands", "; versus &&",
      "  a ; b runs a, then b, no matter what.\n"
      "  a && b runs b only if a succeeded.\n"
      "  a || b runs b only if a failed.\n"
      "Commands report success by exiting with status 0.",
      "Try to cat a file that doesn't exist (nope.txt) and print missing only if that fails.",
      "grep -q '^missing$' \"$OUT\" && [[ \"$CMD\" == *'||'* ]]",
      "cat nope.txt || echo missing",
      "cat nope.txt 2>/dev/null || echo missing", NULL,
      "[[ \"$CMD\" == *'||'* ]] || echo 'Use || between the two commands: the second runs only if the first fails.'; grep -q '^missing$' \"$OUT\" || echo 'missing was not printed.'" },

    { "ctrl-z", QUIZ, "Jobs", "Ctrl-Z",
      "While a command is running in the foreground, the terminal is busy: you can't\n"
      "type another command until it finishes. Ctrl-Z suspends (pauses) the running\n"
      "program and gives you the prompt back. The program is now a job, and\n"
      "  jobs lists them.",
      "You run sleep 60 and press Ctrl-Z. What happens?",
      "b",
      "Suspended is not the same as stopped for good.",
      "The sleep is paused mid-way, listed by jobs as \"suspended\", and you get a prompt.\n"
      "It is not killed and it does not keep counting: it is frozen until you resume it.",
      "a) sleep is killed\nb) sleep is paused and you get a prompt back\nc) sleep keeps running in the background\nd) the terminal closes", NULL },

    { "fg-bg", QUIZ, "Jobs", "fg and bg",
      "A suspended job can be resumed two ways:\n"
      "  fg in the foreground: you are back inside it, as if you never stopped it.\n"
      "  bg in the background: it keeps running but you keep the prompt.\n"
      "Both take a job spec like %1 (job number 1, from jobs). On their own they\n"
      "act on the most recent job.",
      "You suspended a long copy with Ctrl-Z and want it to keep going while you do\n"
      "other things in the same terminal. Which command?",
      "c",
      "You want it running AND you want the prompt.",
      "bg resumes the job in the background: the copy continues and the prompt is yours.\n"
      "fg would also resume it, but then the terminal is busy again until it finishes.",
      "a) fg\nb) jobs\nc) bg\nd) kill %1", NULL },

    { "ampersand", QUIZ, "Jobs", "Starting in the background",
      "Putting & after a command starts it in the background straight away:\n"
      "  ./server &\n"
      "The shell prints its job number and process id and gives you the prompt.\n"
      "Its output still lands in your terminal unless you redirect it.",
      "What does fg %1 do?",
      "d",
      "%1 is a job spec.",
      "fg %1 brings job number 1 into the foreground. Plain fg picks the most recent\n"
      "job; %1, %2 ... choose a specific one from the jobs list.",
      "a) forks the current shell\nb) runs job 1 again from the start\nc) kills job 1\nd) brings job 1 to the foreground", NULL },

    { "chain-suspend", QUIZ, "Jobs", "What exactly gets suspended",
      "Ctrl-Z suspends the program that is running at that instant, not your whole\n"
      "command line. If you typed sleep 8 ; echo done the shell itself runs\n"
      "the sequence, and the shell never suspends: it treats the suspended sleep as\n"
      "finished and moves straight on to echo.",
      "You type sleep 8 ; echo done and press Ctrl-Z after two seconds. What is printed?",
      "b",
      "The ; chain belongs to the shell.",
      "\"done\" appears immediately: the shell moved on to echo as soon as sleep was\n"
      "suspended. The sleep is still there as a suspended job. To suspend the whole\n"
      "sequence as one job, run it as one process: ( sleep 8 ; echo done ).",
      "a) nothing, both are suspended\nb) done, right away, and sleep is left suspended\nc) done, after the remaining 6 seconds\nd) an error", NULL },

    { "kill", QUIZ, "Jobs", "Getting rid of a job",
      "  kill %1 asks job 1 to quit (sends SIGTERM).\n"
      "  kill -9 %1 forces it (SIGKILL) when it ignores the polite request.\n"
      "Ctrl-C does the same as kill for whatever is in the foreground.",
      "jobs shows [1] + suspended sleep 8 left over from earlier. You don't want it.\n"
      "Which command removes it without resuming it in the foreground?",
      "a",
      "You can kill a job by its job spec.",
      "kill %1 ends it. fg %1 would also make it go away eventually, but by resuming\n"
      "it in the foreground and waiting for it to finish.",
      "a) kill %1\nb) fg %1\nc) exit\nd) bg %1", NULL },

    /* ---- second tier ---- */

    { "glob", RUN, "Wildcards", "Match many files at once",
      "The shell expands * before the command runs: *.txt becomes every name here\n"
      "ending in .txt, so cat *.txt prints all of them. ? matches one character.\n"
      "That's why the find lesson quoted its pattern: to stop this expansion.",
      "Print how many lines all the .txt files here have together (wc -l of all of them at once).\n"
      "wc prints a total line when given several files.",
      "[[ \"$CMD\" == *'*'* ]] && grep -q 'total' \"$OUT\"",
      "wc -l with a wildcard.",
      "wc -l *.txt", NULL,
      "[[ \"$CMD\" == *'*'* ]] || echo 'No wildcard used: *.txt stands for every .txt file here.'; grep -q total \"$OUT\" || echo 'No total line: give wc all the .txt files at once, not one.'" },

    { "touch", RUN, "Wildcards", "Empty files and brace expansion",
      "  touch NAME creates an empty file (or just updates the date of an existing one).\n"
      "  {a,b,c} expands to each option in turn: echo file{1,2}.txt prints\n"
      "file1.txt file2.txt. It works anywhere in a command.",
      "Create three empty files at once: draft1.txt, draft2.txt and draft3.txt.",
      "[ -f draft1.txt ] && [ -f draft2.txt ] && [ -f draft3.txt ] && [[ \"$CMD\" == *'{'* ]]",
      "touch draft{1,2,3}.txt",
      "touch draft{1,2,3}.txt", NULL,
      "for f in draft1.txt draft2.txt draft3.txt; do [ -f $f ] || echo \"$f does not exist yet.\"; done; [[ \"$CMD\" == *'{'* ]] || echo 'Do it in one go with braces: draft{1,2,3}.txt'" },

    { "stderr", RUN, "Errors and status", "Errors have their own stream",
      "Commands print normal output on stream 1 (stdout) and errors on stream 2\n"
      "(stderr). > only redirects stream 1; that is why an error message still shows\n"
      "on screen when you redirect. To send errors somewhere: 2> FILE\n"
      "  2>/dev/null throws them away. /dev/null is a file that discards everything.",
      "Run cat nope.txt fruits.txt so the error about nope.txt goes into a file called errors.txt\n"
      "while the fruit list still prints.",
      "grep -q '^banana$' \"$OUT\" && grep -qi 'nope.txt' errors.txt && ! grep -qi 'no such file' \"$OUT\"",
      "... 2> errors.txt",
      "cat nope.txt fruits.txt 2> errors.txt", NULL,
      "[ -e errors.txt ] || echo 'No errors.txt was written: redirect stream 2 with 2> errors.txt.'; grep -qi 'no such file' \"$OUT\" && echo 'The error still printed on screen: > only redirects normal output; errors are stream 2.'; grep -q '^banana$' \"$OUT\" || echo 'The fruit list should still print normally.'" },

    { "status", RUN, "Errors and status", "Exit status",
      "Every command ends with a number: 0 means success, anything else means some\n"
      "kind of failure. The shell keeps the last one in $? and && and || read it.\n"
      "  ls nope ; echo $? prints ls's error, then a non-zero number.",
      "Run grep zzz fruits.txt and then print its exit status on the next line.",
      "[ \"$(tail -n 1 \"$OUT\")\" = \"1\" ] && [[ \"$CMD\" == *'$?'* ]]",
      "grep zzz fruits.txt ; echo $?",
      "grep zzz fruits.txt; echo $?", NULL,
      "[[ \"$CMD\" == *'$?'* ]] || echo 'Print the special variable $? right after grep, on the same line: ; echo $?'" },

    { "cut", RUN, "Text tools", "Pick columns",
      "people.csv has lines like ada,lovelace,1815 — fields separated by commas.\n"
      "  cut -d , -f 2 FILE prints field 2 of each line, using , as the delimiter.\n"
      "  -f 1,3 picks several fields.",
      "Print just the birth years (the third field) from people.csv.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf '1815\\n1912\\n1906')\" ]",
      "cut with -d , and -f 3",
      "cut -d , -f 3 people.csv", NULL,
      "grep -q ',' \"$OUT\" && echo 'Commas are still in the output: tell cut the delimiter is a comma with -d ,'; grep -q 'ada' \"$OUT\" && echo 'Names are showing: pick only field 3 with -f 3.'" },

    { "tr", RUN, "Text tools", "Change characters",
      "  tr A B replaces every A with B in whatever is piped into it. It takes\n"
      "ranges too: tr a-z A-Z upper-cases everything. tr reads only from a pipe\n"
      "or <, never from a file name.",
      "Print fruits.txt in upper case.",
      "grep -q '^BANANA$' \"$OUT\" && grep -q '^CHERRY$' \"$OUT\"",
      "cat fruits.txt | tr a-z A-Z",
      "cat fruits.txt | tr a-z A-Z", NULL,
      "grep -q '^banana$' \"$OUT\" && echo 'Still lower case: pipe the file into tr a-z A-Z.'; [[ \"$CMD\" == *'tr'*'fruits.txt' ]] && echo 'tr does not take a file name: feed the file in with cat ... | tr or with < fruits.txt.'" },

    { "sort-n", RUN, "Text tools", "Sort numbers as numbers",
      "  sort compares text, so 10 comes before 9 (1 is less than 9).\n"
      "  sort -n compares numerically. -r reverses the order.\n"
      "  head -n 1 after a sort gives you the smallest or largest.",
      "Print the single largest number in scores.txt.",
      "[ \"$(cat \"$OUT\")\" = \"97\" ]",
      "sort -n, reversed, then head -n 1",
      "sort -nr scores.txt | head -n 1", NULL,
      "n=$(wc -l < \"$OUT\"); [ \"$n\" -eq 1 ] || echo \"That printed $n lines; only the largest number should appear (head -n 1 after sorting).\"; grep -qx '97' \"$OUT\" || echo 'Not 97: plain sort compares text, so 10 sorts before 9 and 97 before 98. Compare as numbers with -n, and reverse with -r.'" },

    { "tee", RUN, "Text tools", "Save and see at once",
      "  | tee FILE writes what flows through the pipe into FILE and also passes\n"
      "it on, so you can save a result and still see it on screen.",
      "Sort fruits.txt, saving the sorted list to sorted.txt while it also prints on screen.",
      "grep -q '^apple$' \"$OUT\" && [ \"$(cat sorted.txt 2>/dev/null)\" = \"$(sort fruits.txt)\" ]",
      "sort fruits.txt | tee sorted.txt",
      "sort fruits.txt | tee sorted.txt", NULL,
      "[ -e sorted.txt ] || echo 'sorted.txt was not written: put | tee sorted.txt after the sort.'; grep -q '^apple$' \"$OUT\" || echo 'Nothing printed on screen: tee passes the output on as well as saving it; > would swallow it.'" },

    { "for", RUN, "Loops", "Do something for each file",
      "  for f in *.txt; do echo \"$f\"; done\n"
      "runs the part between do and done once per match, with $f set to each name\n"
      "in turn. Any commands can go in the middle, separated by ;.",
      "For every .md file under docs (use find to list them), print its name followed by its line count.\n"
      "Any format is fine as long as each name and its count appear.",
      "[[ \"$CMD\" == *'for '* ]] && grep -q 'readme.md' \"$OUT\" && grep -q 'setup.md' \"$OUT\" && grep -q '3' \"$OUT\"",
      "for f in $(find docs -name '*.md'); do wc -l \"$f\"; done",
      "for f in $(find docs -name '*.md'); do wc -l \"$f\"; done", NULL,
      "[[ \"$CMD\" == *'for '* ]] || echo 'Use a for loop: for f in ...; do ...; done'; grep -q 'setup.md' \"$OUT\" || echo 'setup.md is missing: find docs -name \\'*.md\\' lists every .md file under docs.'" },

    { "xargs", RUN, "Loops", "Turn output into arguments",
      "  find ... | xargs COMMAND runs COMMAND with everything find printed as its\n"
      "arguments, instead of feeding it as input. So find . -name '*.md' | xargs wc -l\n"
      "counts lines in each .md file (and prints a total).",
      "Delete every .log file anywhere under the current directory in one command using find and xargs.\n"
      "There are three: old.log, logs/a.log and logs/b.log.",
      "[[ \"$CMD\" == *xargs* ]] && [ ! -e old.log ] && [ ! -e logs/a.log ] && [ -f fruits.txt ]",
      "find . -name '*.log' | xargs rm",
      "find . -name '*.log' | xargs rm", NULL,
      "[[ \"$CMD\" == *xargs* ]] || echo 'Use xargs: find lists the files, xargs hands them to rm as arguments.'; [ -e logs/a.log ] && echo 'logs/a.log is still there: find must search below the current directory too (start from .).'" },

    { "ln", RUN, "Links and archives", "Symbolic links",
      "  ln -s TARGET NAME makes NAME a symbolic link: a small pointer to TARGET.\n"
      "Opening NAME opens TARGET. ls -l shows links as NAME -> TARGET.\n"
      "Deleting the link leaves the target alone.",
      "Create a link called latest that points to docs/readme.md.",
      "[ -L latest ] && [ \"$(readlink latest)\" = \"docs/readme.md\" ]",
      "ln -s docs/readme.md latest",
      "ln -s docs/readme.md latest", NULL,
      "[ -e latest ] || echo 'Nothing called latest exists yet.'; [ -e latest ] && [ ! -L latest ] && echo 'latest is a copy, not a link: use ln -s.'; [ -L latest ] && [ \"$(readlink latest)\" != docs/readme.md ] && echo \"latest points to $(readlink latest), not docs/readme.md: the target comes first, the link name second.\"" },

    { "tar", RUN, "Links and archives", "Bundle a folder",
      "  tar -czf NAME.tar.gz FOLDER packs FOLDER into one compressed file.\n"
      "  tar -xzf NAME.tar.gz unpacks it. -t instead of -x just lists it.\n"
      "c=create, x=extract, z=gzip, f=file name follows.",
      "Pack the docs folder into docs.tar.gz.",
      "[ -f docs.tar.gz ] && tar -tzf docs.tar.gz | grep -q 'docs/readme.md'",
      "tar -czf docs.tar.gz docs",
      "tar -czf docs.tar.gz docs", NULL,
      "[ -e docs.tar.gz ] || echo 'No docs.tar.gz was created: tar -czf docs.tar.gz <folder>.'" },

    { "which", RUN, "Finding programs", "Where a command lives",
      "Commands are files too. The shell finds them by searching the folders listed\n"
      "in the PATH variable, in order. which NAME prints the one it would use.\n"
      "  echo $PATH shows the list, separated by colons.",
      "Print the full path of the ls program.",
      "grep -q '/ls$' \"$OUT\"",
      "which ls",
      "which ls", NULL,
      "grep -q '/' \"$OUT\" || echo 'The output should be a full path such as /bin/ls: ask with which ls.'" },

    { "man", QUIZ, "Finding programs", "Reading the manual",
      "  man COMMAND opens the manual page for a command: every option, explained.\n"
      "It shows one screen at a time: space for the next page, / to search, q to quit.\n"
      "Most commands also accept --help for a short summary.",
      "You're inside man ls and want to leave it. What do you press?",
      "c",
      "The pager has a one-letter key for quitting.",
      "q quits the pager. Ctrl-C usually works too, but q is the intended way; Ctrl-Z\n"
      "would only suspend it, leaving a job behind.",
      "a) Ctrl-Z\nb) Escape\nc) q\nd) exit", NULL },
};

/* ---------- the second tier: shell-tutor --advanced ---------- */

static const Lesson ADVANCED[] = {
    /* ----- Writing files ----- */

    { "printf", RUN, "Writing files", "printf: text with a shape",
      "echo prints its words and adds a newline. printf is the precise version:\n"
      "it takes a FORMAT first, then the values to put into it.\n"
      "  printf 'FORMAT' VALUE VALUE...\n"
      "In the format, %s stands for the next value (any text), %d for the next\n"
      "value as a whole number, \\n is a line break, \\t a tab. printf adds no\n"
      "newline by itself; you write the \\n. For example\n"
      "  printf '%s has %d legs\\n' cat 4 prints: cat has 4 legs\n"
      "Given more values than the format uses, printf runs the format again for\n"
      "the next ones: printf '%s\\n' a b c prints three lines.",
      "With one printf, print two lines: ada, a tab, 1815; then alan, a tab, 1912.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'ada\\t1815\\nalan\\t1912')\" ] && [[ \"$CMD\" == *printf* ]]",
      "One format with %s, \\t, %d and \\n, followed by four values.",
      "printf '%s\\t%d\\n' ada 1815 alan 1912", NULL,
      "[[ \"$CMD\" == *printf* ]] || echo 'Use printf: its format is what makes the tab and the two lines.'; grep -q '\\\\t' \"$OUT\" && echo 'A literal \\\\t was printed: in printf the escapes go in the FORMAT, and the format goes in single quotes.'; [ \"$(wc -l < \"$OUT\")\" -lt 2 ] && echo 'Only one line came out: the format needs a \\\\n at the end so each pair ends a line.'" },

    { "printf-file", RUN, "Writing files", "A small file in one line",
      "Since printf writes exactly what the format says, line breaks included, it is\n"
      "the quickest way to create a short file with several lines: printf the lines\n"
      "and redirect (>) into the file.\n"
      "  printf 'one\\ntwo\\n' > pair.txt makes a two-line file.",
      "Create shopping.txt containing three lines: eggs, milk, bread.",
      "[ \"$(cat shopping.txt 2>/dev/null)\" = \"$(printf 'eggs\\nmilk\\nbread')\" ]",
      "printf with three words and a \\n after each, redirected into shopping.txt.",
      "printf 'eggs\\nmilk\\nbread\\n' > shopping.txt", NULL,
      "[ -e shopping.txt ] || echo 'No shopping.txt was created: redirect the output into it with >.'; [ -e shopping.txt ] && [ \"$(wc -l < shopping.txt)\" -ne 3 ] && echo \"shopping.txt has $(wc -l < shopping.txt | tr -d ' ') lines, not 3: put a \\\\n after every word.\"" },

    { "heredoc", RUN, "Writing files", "Here-documents: a file typed in place",
      "For anything longer than a line or two, printf gets unreadable. A\n"
      "here-document lets you type the file's contents as they are:\n"
      "  cat > FILE <<'EOF'\n"
      "  first line\n"
      "  second line\n"
      "  EOF\n"
      "Everything between the <<'EOF' line and the line that is just EOF becomes\n"
      "the input of cat, which writes it to FILE. EOF is only a convention; any\n"
      "word works, as long as the same word ends it. The quotes around it mean\n"
      "\"take the text exactly\", so $ signs inside are not expanded.\n"
      "At this prompt, after a line containing << the tutor keeps reading lines\n"
      "(prompt: >) until you type the end word on its own.",
      "Using a here-document, create poem.txt with the lines: roses are red, and: violets are blue.",
      "[[ \"$CMD\" == *'<<'* ]] && [ \"$(cat poem.txt 2>/dev/null)\" = \"$(printf 'roses are red\\nviolets are blue')\" ]",
      "cat > poem.txt <<'EOF', then the two lines, then EOF alone.",
      "cat > poem.txt <<'EOF'\nroses are red\nviolets are blue\nEOF", NULL,
      "[[ \"$CMD\" == *'<<'* ]] || echo 'No here-document there: start with cat > poem.txt <<'\"'\"'EOF'\"'\"' and type the lines on the following prompts.'; [ -e poem.txt ] && grep -q EOF poem.txt && echo 'The end word got into the file: it must be alone on its own line, spelled exactly as after the <<.'" },

    { "quotes", RUN, "Writing files", "Single versus double quotes",
      "Quotes group words, but the two kinds differ in one thing: what happens\n"
      "to $ inside them.\n"
      "  \"double quotes\" still expand variables: \"hello $USER\" contains your name.\n"
      "  'single quotes' take every character literally: 'hello $USER' has a $.\n"
      "Use double quotes around anything with a variable in it, single quotes when\n"
      "you want the text untouched (printf formats, sed and grep patterns).",
      "Set a variable city to Paris, then print exactly: I live in Paris, using the variable inside quotes.",
      "grep -qx 'I live in Paris' \"$OUT\" && [[ \"$CMD\" == *'$city'* || \"$CMD\" == *'${city}'* ]]",
      "city=Paris; echo \"...$city\"",
      "city=Paris; echo \"I live in $city\"", NULL,
      "grep -q '\\$city' \"$OUT\" && echo 'The output shows $city itself: single quotes stop expansion; put the sentence in double quotes.'; [[ \"$CMD\" == *'$city'* ]] || echo 'Use the variable ($city) inside the sentence rather than typing Paris again.'" },

    /* ----- Scripts ----- */

    { "script-write", RUN, "Scripts", "A script from scratch",
      "A script is a file of commands. Three things make it runnable:\n"
      "  1. the first line #!/bin/sh, which names the program that should read\n"
      "     the file (the shebang);\n"
      "  2. execute permission: chmod +x FILE;\n"
      "  3. running it as ./FILE (or from anywhere by its path).\n"
      "Without step 2 you can still run it as sh FILE. Write the file with a\n"
      "here-document. Inside, $(command) works as it does at the prompt.",
      "Write today.sh, which prints: Today is, followed by the output of date, on one line. Make it executable and run it.",
      "[ -x today.sh ] && head -1 today.sh | grep -q '^#!' && grep -q '^Today is .*20[0-9][0-9]' \"$OUT\"",
      "cat > today.sh <<'EOF' with #!/bin/sh and echo \"Today is $(date)\", then chmod +x today.sh; ./today.sh",
      "cat > today.sh <<'EOF'\n#!/bin/sh\necho \"Today is $(date)\"\nEOF\nchmod +x today.sh; ./today.sh", NULL,
      "[ -e today.sh ] || echo 'There is no today.sh yet: create it first, with a here-document.'; [ -e today.sh ] && ! head -1 today.sh | grep -q '^#!' && echo 'The first line of today.sh should be the shebang: #!/bin/sh'; [ -e today.sh ] && [ ! -x today.sh ] && echo 'today.sh is not executable yet: chmod +x today.sh'; grep -qi 'permission denied' \"$OUT\" && echo 'Permission denied means the execute bit is missing: chmod +x today.sh'; [ -x today.sh ] && ! grep -q '^Today is' \"$OUT\" && echo 'The script exists and is executable; now run it: ./today.sh'" },

    { "args", RUN, "Scripts", "Arguments: $1, $2, $#",
      "Words typed after a script's name reach it as arguments. Inside the script\n"
      "  $1 is the first argument, $2 the second, and so on;\n"
      "  $# is how many there are; \"$@\" is all of them.\n"
      "So sh greet.sh Ann Bob runs greet.sh with $1=Ann and $2=Bob. The one-line\n"
      "way to write a short script: printf '#!/bin/sh\\necho ...\\n' > FILE.",
      "Write hello.sh so that it prints Hello, NAME! where NAME is its first argument. Run it with the argument World.",
      "grep -qx 'Hello, World!' \"$OUT\" && grep -q '\\$1' hello.sh",
      "In the script: echo \"Hello, $1!\"  Then run it with World after its name.",
      "printf '#!/bin/sh\\necho \"Hello, $1!\"\\n' > hello.sh; sh hello.sh World", NULL,
      "grep -q 'Hello from a script' \"$OUT\" && echo 'That is the old hello.sh from the fixtures; overwrite it with your own.'; [ -e hello.sh ] && ! grep -q '\\$1' hello.sh && echo 'The script never looks at $1, so the name cannot get in.'; grep -q '^Hello, !$' \"$OUT\" && echo 'Hello, ! means $1 was empty: put World after the script name when you run it.'" },

    { "read", RUN, "Scripts", "Asking for input",
      "  read NAME waits for a line of input and stores it in the variable NAME.\n"
      "Print the question first, then read:\n"
      "  printf 'Your age? '\n"
      "  read age\n"
      "  echo \"$age, noted.\"\n"
      "At this prompt nothing can type into a running command, so supply the\n"
      "answer through a pipe: echo 42 | ./ask.sh feeds 42 to the read.",
      "Write ask.sh that asks for a name, reads it and prints: Nice to meet you, NAME. Run it with the name Ada supplied through a pipe.",
      "grep -q 'Nice to meet you, Ada\\.' \"$OUT\" && grep -q 'read' ask.sh",
      "In the script: read name, then echo \"Nice to meet you, $name.\"  Run: echo Ada | sh ask.sh",
      "cat > ask.sh <<'EOF'\n#!/bin/sh\nprintf 'What is your name? '\nread name\necho \"Nice to meet you, $name.\"\nEOF\necho Ada | sh ask.sh", NULL,
      "[ -e ask.sh ] || echo 'No ask.sh yet.'; [ -e ask.sh ] && grep -q read ask.sh && [ ! -s \"$OUT\" ] && echo 'ask.sh is written. Now run it with the name piped in: echo Ada | sh ask.sh'; [ -e ask.sh ] && ! grep -q read ask.sh && echo 'The script needs a read command to take the answer in.'; grep -q 'Nice to meet you, \\.' \"$OUT\" && echo 'The name came out empty: nothing was piped in. Run it as echo Ada | sh ask.sh'" },

    { "if", RUN, "Scripts", "if, then, else",
      "  if COMMAND; then\n"
      "    ...commands for yes...\n"
      "  else\n"
      "    ...commands for no...\n"
      "  fi\n"
      "if runs COMMAND and looks at its exit status: 0 means yes. The usual\n"
      "command to test things is [ (a real command, so it needs spaces around\n"
      "everything inside, and a closing ]):\n"
      "  [ -f FILE ] FILE exists and is a file; [ -d FILE ] it is a directory\n"
      "  [ \"$a\" = \"$b\" ] the two texts are the same; != different\n"
      "  [ -z \"$a\" ] $a is empty\n"
      "The else part is optional. Indentation is only for reading.",
      "Write check.sh that prints found if a file called notes.txt exists in the current directory, and missing otherwise. Run it.",
      "grep -qx found \"$OUT\" && grep -q 'if ' check.sh && grep -q 'else' check.sh && grep -q 'fi' check.sh",
      "if [ -f notes.txt ]; then echo found; else echo missing; fi",
      "cat > check.sh <<'EOF'\n#!/bin/sh\nif [ -f notes.txt ]; then\n  echo found\nelse\n  echo missing\nfi\nEOF\nsh check.sh", NULL,
      "[ -e check.sh ] && [ ! -s \"$OUT\" ] && echo 'check.sh is written; now run it: sh check.sh'; grep -q 'missing argument\\|unary operator\\|\\[: ' \"$OUT\" && echo 'A [ error: check the spaces. [ -f notes.txt ] needs a space after [ and before ].'; grep -q 'syntax error' \"$OUT\" && echo 'A syntax error: the shape is if ...; then ... else ... fi, and fi is required.'; grep -qx missing \"$OUT\" && echo 'It said missing, but notes.txt is here: the test should be -f notes.txt'" },

    { "tests", QUIZ, "Scripts", "Comparing numbers",
      "[ compares text with = and !=. Numbers have their own operators, because\n"
      "as text \"9\" sorts after \"10\":\n"
      "  -eq equal, -ne not equal, -lt less than, -le at most, -gt, -ge.\n"
      "Never use < or > inside [ ]: to the shell those are redirections, so\n"
      "[ $n < 10 ] tries to read a file called 10.",
      "You want to test whether $n is less than 10. Which line is right?",
      "b",
      "< is a redirection, even inside [ ].",
      "-lt is \"less than\" for numbers. The quotes around \"$n\" keep [ from breaking when n is empty.",
      "a) if [ $n < 10 ]; then\nb) if [ \"$n\" -lt 10 ]; then\nc) if [ \"$n\" lt 10 ]; then\nd) if $n < 10; then", NULL },

    { "arith", RUN, "Scripts", "Arithmetic",
      "The shell treats everything as text; to calculate, wrap the expression in\n"
      "$(( )): $((3 * 4)) becomes 12. It knows + - * / and %, and variables\n"
      "inside it need no $: n=5; echo $((n + 1)) prints 6. Whole numbers only.",
      "Print the sum of 17 and 25 using shell arithmetic.",
      "grep -qx 42 \"$OUT\" && [[ \"$CMD\" == *'$(('* ]]",
      "echo $((...))",
      "echo $((17 + 25))", NULL,
      "grep -q '17 + 25\\|17+25' \"$OUT\" && echo 'The expression was printed, not computed: it has to be inside $(( )).'" },

    { "while-read", RUN, "Scripts", "A loop over lines",
      "You know for FILE in *; do ...; done. To loop over the lines of a file:\n"
      "  while read line; do\n"
      "    echo \"got: $line\"\n"
      "  done < FILE\n"
      "read takes one line per turn and fails at the end of the file, which ends\n"
      "the loop. The < FILE at the end feeds the whole loop.",
      "Print each line of fruits.txt with fruit: in front (fruit: apple, and so on), using a while read loop.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'fruit: apple\\nfruit: banana\\nfruit: cherry')\" ] && [[ \"$CMD\" == *while* ]]",
      "while read f; do echo \"fruit: $f\"; done < fruits.txt",
      "while read f; do echo \"fruit: $f\"; done < fruits.txt", NULL,
      "[[ \"$CMD\" == *while* ]] || echo 'This one wants a while read loop.'; [[ \"$CMD\" == *'<'* ]] || echo 'Nothing was fed into the loop: put < fruits.txt after done.'" },

    { "functions", RUN, "Scripts", "Functions",
      "A function is a named block of commands, defined once and called like a\n"
      "command:\n"
      "  hello() { echo \"hello, $1\"; }\n"
      "  hello world\n"
      "Inside, $1 $2... are the function's own arguments. The braces need a space\n"
      "after { and a ; before } when it is all on one line.",
      "Define a function shout that prints its first argument in capital letters, then call it with the word hello.",
      "grep -qx HELLO \"$OUT\" && [[ \"$CMD\" == *'()'* ]]",
      "shout() { echo \"$1\" | tr a-z A-Z; }; shout hello",
      "shout() { echo \"$1\" | tr a-z A-Z; }; shout hello", NULL,
      "[[ \"$CMD\" == *'()'* ]] || echo 'Define a function first: name() { ...; }'; grep -qx hello \"$OUT\" && echo 'It printed in lower case: pipe the text through tr a-z A-Z.'; grep -q 'parse error\\|syntax error' \"$OUT\" && echo 'Check the braces: a space after { and a ; before }.'" },

    { "exit", RUN, "Scripts", "Exit status from a script",
      "A script ends with the status of its last command, or with exit N to say\n"
      "so explicitly: exit 0 for success, anything else for failure. Right after\n"
      "a command, $? holds its exit status. And set -e at the top of a script\n"
      "makes it stop at the first command that fails, instead of blundering on.",
      "Write fail.sh that prints oops and exits with status 3. Run it, then print the status it returned.",
      "grep -qx oops \"$OUT\" && grep -qx 3 \"$OUT\" && grep -q 'exit 3' fail.sh",
      "In the script: echo oops, then exit 3. After running it: echo $?",
      "printf '#!/bin/sh\\necho oops\\nexit 3\\n' > fail.sh; sh fail.sh; echo $?", NULL,
      "[ -e fail.sh ] && ! grep -q 'exit 3' fail.sh && echo 'fail.sh needs an exit 3 line.'; grep -qx oops \"$OUT\" && ! grep -qx 3 \"$OUT\" && echo 'Now print the status: echo $? right after running the script, on the same line.'" },

    { "case", RUN, "Scripts", "case: many branches",
      "For one value with several possible forms, case is tidier than a chain\n"
      "of ifs:\n"
      "  case \"$1\" in\n"
      "    start|go) echo starting ;;\n"
      "    stop)     echo stopping ;;\n"
      "    *)        echo \"unknown: $1\" ;;\n"
      "  esac\n"
      "Each branch is a pattern (| separates alternatives, * matches anything),\n"
      "a ), the commands, and ;; to end it.",
      "Write yn.sh that prints yes for the argument y or yes, no for n or no, and what? for anything else, using case. Run it with the argument y.",
      "grep -qx yes \"$OUT\" && grep -q 'case' yn.sh && grep -q 'esac' yn.sh",
      "case \"$1\" in y|yes) ... ;; n|no) ... ;; *) ... ;; esac",
      "cat > yn.sh <<'EOF'\n#!/bin/sh\ncase \"$1\" in\n  y|yes) echo yes ;;\n  n|no)  echo no ;;\n  *)     echo 'what?' ;;\nesac\nEOF\nsh yn.sh y", NULL,
      "[ -e yn.sh ] && [ ! -s \"$OUT\" ] && echo 'yn.sh is written; now run it with y as the argument: sh yn.sh y'; [ -e yn.sh ] && ! grep -q esac yn.sh && echo 'case ends with esac (case backwards).'; grep -q 'syntax error' \"$OUT\" && echo 'Each branch is PATTERN) commands ;;'; grep -qx 'what?' \"$OUT\" && echo 'It fell through to *: the y branch did not match. Was the argument y, and is the branch y|yes)?'" },

    /* ----- Your shell setup ----- */

    { "export", RUN, "Your shell setup", "Environment variables",
      "A variable set with NAME=value belongs to this shell only. Programs the\n"
      "shell starts get a copy of the environment: the variables marked with\n"
      "export. HOME, USER and PATH are environment variables; env lists them all.\n"
      "  export NAME=value sets and exports in one go.\n"
      "By convention environment variables are UPPER CASE.",
      "Set GREETING to hello and export it, then run sh -c 'echo $GREETING' to show a child program sees it.",
      "grep -qx hello \"$OUT\" && [[ \"$CMD\" == *export* ]]",
      "export GREETING=hello; then the sh -c command from the task.",
      "export GREETING=hello; sh -c 'echo $GREETING'", NULL,
      "[[ \"$CMD\" == *export* ]] || echo 'Without export the child sh gets no GREETING, so it prints an empty line.'" },

    { "path", RUN, "Your shell setup", "PATH: where commands are found",
      "When you type a command name, the shell looks for a program of that name\n"
      "in each folder listed in PATH, in order, separated by colons:\n"
      "  echo $PATH\n"
      "That is why ./script.sh needs the ./ (the current folder is not in PATH)\n"
      "and why your own commands live in a folder such as ~/bin: add it to the\n"
      "front and they run by name from anywhere:\n"
      "  PATH=\"$HOME/bin:$PATH\"\n"
      "Here there is a bin folder with an executable called hi inside.",
      "Put this folder's bin at the front of PATH, then run hi by its name alone.",
      "grep -q 'hi from bin' \"$OUT\" && [[ \"$CMD\" == *PATH* ]] && [[ \"$CMD\" != *'bin/hi'* ]]",
      "PATH=\"$PWD/bin:$PATH\"; hi",
      "PATH=\"$PWD/bin:$PATH\"; hi", NULL,
      "[[ \"$CMD\" == *'bin/hi'* ]] && echo 'That runs it by path. The point is to run plain hi, after putting bin in PATH.'; grep -q 'command not found' \"$OUT\" && echo 'hi was not found: PATH must contain the bin folder here, e.g. PATH=\"$PWD/bin:$PATH\", on the same line since each line is a fresh shell.'" },

    { "alias", RUN, "Your shell setup", "Aliases",
      "An alias is a short name for a longer command:\n"
      "  alias ll='ls -la'\n"
      "From the next line on, ll runs ls -la. Not on the same line: the shell\n"
      "reads a whole line before it looks up aliases, so an alias defined and used\n"
      "on one line is not found yet. alias on its own lists the aliases you have.\n"
      "Since each line here is a fresh shell, confirm it with the list instead.",
      "Make an alias ll for ls -la, then show the list of aliases to confirm it is there.",
      "[[ \"$CMD\" == *'alias ll='* ]] && grep -q \"ll=\" \"$OUT\"",
      "alias ll='ls -la'; alias",
      "alias ll='ls -la'; alias", NULL,
      "[[ \"$CMD\" == *'alias ll='* ]] || echo 'Start with alias ll=...'; grep -q 'command not found: ll' \"$OUT\" && echo 'As the lesson says, ll is not known on the line that defines it. Show the alias list instead: alias'; [[ \"$CMD\" == *'alias ll='* ]] && [ ! -s \"$OUT\" ] && echo 'The alias was defined but nothing was printed: add ; alias to list them.'" },

    { "zshrc", QUIZ, "Your shell setup", "Making it stick: ~/.zshrc",
      "Aliases, PATH changes and exported variables vanish with the shell that\n"
      "made them. To have them in every terminal, put the same lines into the\n"
      "file ~/.zshrc: zsh reads it every time a new interactive shell starts.\n"
      "(bash uses ~/.bashrc.) A shell that is already open does not notice the\n"
      "change; run source ~/.zshrc in it, or open a new terminal window.",
      "You add alias ll='ls -la' to ~/.zshrc, but the terminal window you already have open says: command not found: ll. Why?",
      "b",
      "When is ~/.zshrc read?",
      "~/.zshrc is read once, when a shell starts. This shell started before the line was added. source ~/.zshrc reads it now; new windows get it automatically.",
      "a) aliases cannot go in ~/.zshrc\nb) the file is read when a shell starts, and this one started earlier: run source ~/.zshrc or open a new window\nc) the Mac must be restarted\nd) ~/.zshrc is only for PATH", NULL },

    { "history", QUIZ, "Your shell setup", "Getting commands back",
      "The shell remembers what you typed. The up arrow walks back through it,\n"
      "history prints the list, !! repeats the last command (sudo !! is a common\n"
      "use), and Ctrl-R searches: press it, type part of an old command, and the\n"
      "matching line appears; press Enter to run it, Ctrl-R again for older matches.",
      "You ran a long command ten minutes ago and want it back without retyping it. The quickest way?",
      "a",
      "Search, don't scroll.",
      "Ctrl-R searches backwards through your history as you type. The other three work but take longer.",
      "a) press Ctrl-R and type a few letters from it\nb) open ~/.zsh_history in an editor\nc) press the up arrow until it appears\nd) type it again", NULL },

    /* ----- Permissions ----- */

    { "ls-l-perms", QUIZ, "Permissions", "Reading the permission letters",
      "The first column of ls -l, such as -rwxr-x---, is ten characters: the\n"
      "file type (- file, d directory, l link) and then three groups of three:\n"
      "  rwx  for the owner (you, for your files)\n"
      "  r-x  for the file's group\n"
      "  ---  for everyone else\n"
      "r read, w write (change or delete), x execute (run it; for a directory,\n"
      "enter it). A - means that right is missing.",
      "A file shows -rwxr-x---. Who can run it?",
      "b",
      "Three groups: owner, group, others.",
      "Owner has rwx, the group has r-x (x included), others have nothing at all, not even read.",
      "a) everyone\nb) the owner and members of the file's group\nc) only the owner\nd) nobody, it is a directory", NULL },

    { "chmod", RUN, "Permissions", "Changing permissions",
      "chmod changes those letters. Two spellings:\n"
      "  chmod u+x FILE adds (+) execute (x) for the user/owner (u); g is group,\n"
      "  o others, a all. chmod go-w FILE removes write from group and others.\n"
      "  chmod 644 FILE sets all three at once with a digit each: r=4, w=2, x=1,\n"
      "  added up. 6 is rw-, 4 is r--, 7 is rwx, 0 is ---. So 644 is rw-r--r--,\n"
      "  755 is rwxr-xr-x.\n"
      "Here private.txt is readable by everyone.",
      "Make private.txt readable and writable by you, and not accessible to anyone else.",
      "[ \"$(stat -f %Lp private.txt 2>/dev/null || stat -c %a private.txt)\" = 600 ]",
      "rw for you is 6, nothing for group and others is 0 0. Or chmod go-rw.",
      "chmod 600 private.txt", NULL,
      "m=$(stat -f %Lp private.txt 2>/dev/null || stat -c %a private.txt); [ \"$m\" = 644 ] && echo 'Unchanged (644): group and others can still read it.'; [ \"$m\" != 644 ] && [ \"$m\" != 600 ] && echo \"It is now $m; the target is 600: rw for you, nothing for the rest.\"" },

    { "sudo", QUIZ, "Permissions", "sudo",
      "Some files belong to the system, not to you, and some actions (installing\n"
      "software system-wide, changing settings under /etc) need the administrator.\n"
      "sudo COMMAND runs COMMAND as the administrator after asking for your\n"
      "password. It removes every safety net, so it is for the specific cases that\n"
      "need it, never a reflex to make an error go away.",
      "When is putting sudo in front of a command the right move?",
      "b",
      "Understand first, escalate second.",
      "Permission denied on a system file, and you know why it needs system-level rights: that is sudo's job. On your own files it is never needed; a permission problem there means chmod, not sudo.",
      "a) whenever a command prints an error, to be safe\nb) when a command needs system-level access and you understand why\nc) never; it is disabled on a Mac\nd) for any command that changes a file", NULL },

    /* ----- Patterns: regex, sed, awk ----- */

    { "regex", RUN, "Patterns", "Regular expressions",
      "grep's search text is a regular expression (regex): a pattern where a few\n"
      "characters have a meaning of their own.\n"
      "  .      any one character          ^      the start of the line\n"
      "  *      the previous thing, repeated any number of times (also zero)\n"
      "  $      the end of the line         [abc]  one of a, b or c; [0-9] a digit\n"
      "So 'b.t' matches bat and bit, '^b' lines that start with b, 't$' lines\n"
      "ending in t, '^[0-9]' lines starting with a digit. Put the pattern in\n"
      "single quotes so the shell leaves it alone. words.txt is the file to try on.",
      "Print the lines of words.txt that start with ca.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'cat\\ncar\\ncart')\" ]",
      "^ anchors the pattern to the start.",
      "grep '^ca' words.txt", NULL,
      "grep -q '^batch$' \"$OUT\" && echo 'batch came through: it contains ca but does not start with it. Anchor the pattern with ^.'; [ -s \"$OUT\" ] || echo 'Nothing matched.'" },

    { "grep-E", RUN, "Patterns", "Extended patterns",
      "grep -E turns on a few more operators:\n"
      "  +   the previous thing, one or more times    ?   optional\n"
      "  |   either side                              ( ) group\n"
      "  {3} exactly three times\n"
      "For instance '^[0-9]+$' is a line made only of digits, one or more;\n"
      "'cat|dog' is a line with either; '^ca(t|r)$' is exactly cat or car.",
      "Print the lines of words.txt that consist of digits only.",
      "[ \"$(cat \"$OUT\")\" = 2024 ]",
      "Start of line, a digit repeated one or more times, end of line. Needs -E.",
      "grep -E '^[0-9]+$' words.txt", NULL,
      "grep -q abc123 \"$OUT\" && echo 'abc123 came through: anchor both ends, ^ and $, so letters cannot sneak in.'; [ -s \"$OUT\" ] || echo 'Nothing matched. Without -E, + is an ordinary character; with -E it means one or more.'" },

    { "sed-s", RUN, "Patterns", "sed: replace text",
      "sed reads lines, applies an editing command and prints the result. The\n"
      "one everybody uses is substitute:\n"
      "  sed 's/OLD/NEW/' FILE replaces the first OLD on each line with NEW.\n"
      "  sed 's/OLD/NEW/g' FILE replaces every one (g for global).\n"
      "OLD is a regular expression. The file itself is not changed; the result\n"
      "is printed. letter.txt is a form letter with NAME and NUMBER to fill in.",
      "Print letter.txt with every NAME replaced by Grace. Leave the file itself as it is.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf 'Dear Grace,\\nYour order NUMBER has shipped.\\nThanks again, Grace')\" ] && grep -q '^Dear NAME,' letter.txt",
      "sed 's/NAME/Grace/g' letter.txt",
      "sed 's/NAME/Grace/g' letter.txt", NULL,
      "[ -e letter.txt ] || echo 'letter.txt is gone: type reset for a fresh copy.'; [ -e letter.txt ] && ! grep -q '^Dear NAME,' letter.txt && echo 'The file itself was changed; this task only prints the replaced text. Type reset for a fresh copy.'; grep -q 'Thanks again, NAME' \"$OUT\" && echo 'The second NAME on the last line survived: add g after the last / to replace all of them.'" },

    { "sed-i", RUN, "Patterns", "sed: change the file itself",
      "To edit the file in place instead of printing, add -i. On a Mac the\n"
      "option needs an argument, the suffix for a backup copy, and '' means no\n"
      "backup:\n"
      "  sed -i '' 's/OLD/NEW/' FILE     (macOS)\n"
      "  sed -i 's/OLD/NEW/' FILE        (Linux)\n"
      "Test the substitution without -i first; there is no undo.",
      "Change NUMBER to 42 inside letter.txt itself.",
      "grep -q 'order 42 has' letter.txt",
      "The same s command as before, with -i '' in front of it.",
      "sed -i '' 's/NUMBER/42/' letter.txt", NULL,
      "grep -q 'order 42 has' \"$OUT\" && echo 'The result was printed but letter.txt is unchanged: that needs -i.'; grep -q 'invalid command code\\|extra characters' \"$OUT\" && echo \"On a Mac -i needs its backup-suffix argument: sed -i '' ...\"" },

    { "sed-lines", RUN, "Patterns", "sed: pick or drop lines",
      "sed can also select lines. -n stops the automatic printing; p prints:\n"
      "  sed -n '2p' FILE      only line 2\n"
      "  sed -n '3,5p' FILE    lines 3 to 5\n"
      "  sed '1d' FILE         everything except line 1 (d deletes)\n"
      "  sed '/draft/d' FILE   drop the lines matching a pattern",
      "Using sed, print only lines 3 to 5 of numbers.txt.",
      "[ \"$(cat \"$OUT\")\" = \"$(printf '3\\n4\\n5')\" ] && [[ \"$CMD\" == *sed* ]]",
      "-n with a range and p.",
      "sed -n '3,5p' numbers.txt", NULL,
      "[[ \"$CMD\" == *sed* ]] || echo 'This one wants sed (head and tail could do it too).'; [ \"$(wc -l < \"$OUT\")\" -gt 3 ] && echo 'Too many lines: without -n, sed prints every line and then the selected ones again.'" },

    { "awk", RUN, "Patterns", "awk: columns and sums",
      "awk reads each line, splits it into fields ($1, $2, ... and $NF for the\n"
      "last), and runs a small program on it:\n"
      "  awk '{ print $2 }' FILE           second column of every line\n"
      "  awk -F, '{ print $1 }' FILE       fields separated by commas\n"
      "  awk '{ total += $1 } END { print total }' FILE\n"
      "The END block runs once after the last line: the place to print a total.\n"
      "scores.txt has one number per line.",
      "Print the sum of the numbers in scores.txt, using awk.",
      "grep -qx 165 \"$OUT\" && [[ \"$CMD\" == *awk* ]]",
      "Add $1 to a variable on every line; print it in END.",
      "awk '{ total += $1 } END { print total }' scores.txt", NULL,
      "[[ \"$CMD\" == *awk* ]] || echo 'This one wants awk.'; [ \"$(wc -l < \"$OUT\")\" -gt 1 ] && echo 'A number per line came out: the print belongs in the END block, so it runs once at the end.'" },

    /* ----- Processes ----- */

    { "kill", RUN, "Processes", "Finding and stopping a process",
      "Every running program is a process with a number, its PID.\n"
      "  ps aux lists them all; ps aux | grep NAME finds one.\n"
      "  pgrep NAME prints just the PIDs of matching processes.\n"
      "  kill PID asks a process to quit.\n"
      "After starting something in the background with &, $! is its PID.",
      "Start sleep 300 in the background, then stop it with kill, using its PID.",
      "[[ \"$CMD\" == *kill* ]] && [[ \"$CMD\" == *'&'* ]] && ! pgrep -f 'sleep 300' >/dev/null",
      "sleep 300 & then kill $!",
      "sleep 300 & kill $!", NULL,
      "[[ \"$CMD\" == *'&'* ]] || echo 'Start the sleep in the background with & first, on the same line.'; [[ \"$CMD\" == *kill* ]] || echo 'Now kill it: kill $! uses the PID of the last background command.'" },

    { "signals", QUIZ, "Processes", "Signals",
      "kill sends a signal. The default, TERM, is a polite request to quit, which\n"
      "a program may handle (save its work) or ignore. Ctrl-C sends INT, similar.\n"
      "  kill -9 PID sends KILL, which cannot be caught or ignored: the process\n"
      "is gone at once, without cleaning up. Use it only when TERM did nothing.",
      "kill 1234 did nothing; the program is still there a minute later. What now?",
      "a",
      "Escalate.",
      "kill -9 is the one signal a stuck program cannot ignore. It is the last resort because the program gets no chance to save or clean up.",
      "a) kill -9 1234\nb) kill 1234 again, a few times\nc) restart the computer\nd) kill -0 1234", NULL },

    /* ----- Handy tools ----- */

    { "both-streams", RUN, "Handy tools", "Both streams into one file",
      "You know > for normal output and 2> for errors. To send both to the same\n"
      "place, redirect output, then send stream 2 to where stream 1 goes:\n"
      "  COMMAND > FILE 2>&1\n"
      "The order matters: 2>&1 means \"2 goes where 1 goes now\", so it comes\n"
      "after > FILE. This is how you capture everything a command says.",
      "Run ls nope docs so that both its listing and its error message end up in all.txt.",
      "grep -q 'No such file' all.txt && grep -q 'readme.md' all.txt && [ ! -s \"$OUT\" ]",
      "> all.txt and then 2>&1, in that order.",
      "ls nope docs > all.txt 2>&1", NULL,
      "[ -e all.txt ] || echo 'No all.txt was made.'; [ -e all.txt ] && ! grep -q 'No such file' all.txt && echo 'The error message did not go into the file: add 2>&1 after > all.txt'; [ -e all.txt ] && ! grep -q readme.md all.txt && echo 'The listing did not go into the file: that is > all.txt'; [ -s \"$OUT\" ] && grep -q 'No such' \"$OUT\" && echo 'The error still showed on screen: 2>&1 must come after > all.txt, not before.'" },

    { "diff", RUN, "Handy tools", "Comparing two files",
      "  diff OLD NEW shows the lines that differ: < lines are from the first\n"
      "file, > lines from the second, with the line numbers in between. No\n"
      "output means the files are identical. diff -u is the unified format\n"
      "that git and code reviews use (- and + lines). v1.txt and v2.txt are\n"
      "two versions of the same text.",
      "Show the differences between v1.txt and v2.txt.",
      "[[ \"$CMD\" == *diff* ]] && grep -q 'cherry\\|orange' \"$OUT\"",
      "diff, then the two file names.",
      "diff v1.txt v2.txt", NULL,
      "[[ \"$CMD\" == *diff* ]] || echo 'The command is diff.'" },

    { "du", RUN, "Handy tools", "Disk space",
      "  du -sh FOLDER  the size of a folder and everything in it (s: one\n"
      "                 summary line, h: human units like 4.0K, 12M, 1.5G)\n"
      "  du -sh *       one line per item in the current folder\n"
      "  df -h          how full each disk is",
      "Show how much space the docs folder takes, as a single human-readable line.",
      "[[ \"$CMD\" == *du* ]] && [ \"$(wc -l < \"$OUT\")\" -eq 1 ] && grep -q docs \"$OUT\"",
      "du with -s and -h, then the folder.",
      "du -sh docs", NULL,
      "[[ \"$CMD\" == *du* ]] || echo 'The command is du.'; [ \"$(wc -l < \"$OUT\")\" -gt 1 ] && echo 'Several lines came out: -s gives one summary line.'" },

    { "date", RUN, "Handy tools", "Dates in the shape you want",
      "date prints the current date and time. Give it a format, after a +, to\n"
      "choose the shape: %Y year, %m month, %d day, %H hours, %M minutes.\n"
      "  date +%Y-%m-%d       2026-09-20\n"
      "  date +%H:%M          14:05\n"
      "%F is short for %Y-%m-%d. Handy in file names: backup-$(date +%F).tar",
      "Print today's date as year-month-day, with the numbers separated by dashes.",
      "grep -qx \"$(date +%F)\" \"$OUT\"",
      "date +%Y-%m-%d",
      "date +%Y-%m-%d", NULL,
      "grep -q '^[A-Z][a-z][a-z] ' \"$OUT\" && echo 'That is the default format; give date a +FORMAT.'" },

    { "ssh", QUIZ, "Handy tools", "Other computers: ssh and scp",
      "  ssh USER@HOST opens a shell on another computer; everything you know\n"
      "works there. exit comes back.\n"
      "  scp FILE USER@HOST:PATH copies a file there; swap the two to copy\n"
      "back. The colon separates the machine from the path on it; a colon with\n"
      "nothing after it means the home folder.",
      "Copy report.pdf from this Mac into the home folder of user ann on the machine box.example.com. Which command?",
      "a",
      "scp, source first, then destination with a colon.",
      "scp copies; the source comes first; the destination is user@host: with the path after the colon, and nothing after the colon means home.",
      "a) scp report.pdf ann@box.example.com:\nb) ssh report.pdf ann@box.example.com\nc) cp report.pdf ann@box.example.com\nd) scp ann@box.example.com: report.pdf", NULL },
};

#define MAX_LESSONS 64
static const Lesson *LESSONS = BASIC;
static int LESSON_COUNT = (int)(sizeof BASIC / sizeof BASIC[0]);
static int advanced;   /* --advanced: the second lesson set, with its own progress file */

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
    "mkdir -p logs && printf 'a\\n' > logs/a.log && printf 'b\\n' > logs/b.log\n"
    /* used by the advanced tier */
    "mkdir -p bin && printf '#!/bin/sh\\necho hi from bin\\n' > bin/hi && chmod +x bin/hi\n"
    "printf 'nothing to see\\n' > private.txt && chmod 644 private.txt\n"
    "printf 'cat\\ncar\\ncart\\nbat\\nbatch\\ndog\\nabc123\\n2024\\n' > words.txt\n"
    "printf 'Dear NAME,\\nYour order NUMBER has shipped.\\nThanks again, NAME\\n' > letter.txt\n"
    "printf 'apple\\nbanana\\ncherry\\n' > v1.txt && printf 'apple\\nbanana\\norange\\n' > v2.txt\n";

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
static char diag_file[PATH_MAX];      /* scratch_root/diag: what a lesson's diagnosis printed */

static void become_shell(const char *script, const char *dir, const char *cmd_text) {
    if (chdir(dir) != 0) { fprintf(stderr, "shell-tutor: the scratch folder %s is gone.\n", dir); _exit(126); }
    setenv("OUT", out_file, 1);
    setenv("CMD", cmd_text ? cmd_text : "", 1);
    signal(SIGINT, SIG_DFL);   /* the tutor ignores Ctrl-C; the command must not */
    alarm(COMMAND_TIMEOUT_SEC);
    execl(SHELL, "zsh", "-f", "-c", script, (char *)NULL);
    _exit(127);
}

static int exit_code(int status) {
    return WIFSIGNALED(status) ? -1 : WEXITSTATUS(status);
}

/*
 * Runs `script` with zsh in `dir`, output captured to `capture` (or discarded
 * when NULL). Used for setup, checks and diagnoses. Returns the exit status,
 * or -1 if it had to be killed.
 */
static int run_shell(const char *script, const char *dir, const char *capture, const char *cmd_text) {
    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (pid == 0) {
        int fd = open(capture ? capture : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) _exit(126);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
        int in = open("/dev/null", O_RDONLY);
        if (in >= 0) { dup2(in, STDIN_FILENO); close(in); }
        become_shell(script, dir, cmd_text);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return exit_code(status);
}

/*
 * Runs the user's command inside a pseudo-terminal the width of the real one,
 * so programs behave as they do in a terminal window: ls prints in columns,
 * for instance. Everything it prints is copied to `capture` with the
 * terminal's \r\n turned back into \n.
 */
static int run_in_terminal(const char *script, const char *dir, const char *capture, const char *cmd_text) {
    struct winsize ws = { 24, 80, 0, 0 };
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    if (ws.ws_col < 20) ws.ws_col = 80;
    int master;
    pid_t pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) return run_shell(script, dir, capture, cmd_text);
    if (pid == 0) become_shell(script, dir, cmd_text);

    /* Nothing will type into this terminal: hand any command that reads its
       input an immediate end-of-file, as /dev/null would. */
    struct termios t;
    if (tcgetattr(master, &t) == 0) { t.c_lflag &= ~ECHO; tcsetattr(master, TCSANOW, &t); }
    write(master, "\004", 1);

    FILE *out = fopen(capture, "w");
    int status = 0, exited = 0;
    char buf[4096];
    for (;;) {
        struct pollfd pfd = { master, POLLIN, 0 };
        int ready = poll(&pfd, 1, 200);
        if (ready > 0) {
            ssize_t n = read(master, buf, sizeof buf);
            if (n <= 0) break;               /* EIO once the last process on the terminal is gone */
            if (out) for (ssize_t k = 0; k < n; k++) if (buf[k] != '\r') fputc(buf[k], out);
            continue;
        }
        if (!exited && waitpid(pid, &status, WNOHANG) == pid) exited = 1;
        if (exited) break;                   /* zsh is gone (perhaps killed by the alarm); don't hang on orphans */
    }
    if (out) fclose(out);
    close(master);
    if (!exited) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return exit_code(status);
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
    snprintf(diag_file, sizeof diag_file, "%s/diag", scratch_root);
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
            printf("%s... (output cut after %d lines)%s\n", DIM, MAX_OUTPUT_LINES, RESET);
            break;
        }
        printf("%s%s%s%s", DIM, line, line[strlen(line) - 1] == '\n' ? "" : "\n", RESET);
    }
    fclose(f);
    if (status == -1) printf("%s(stopped after %d seconds)%s\n", RED, COMMAND_TIMEOUT_SEC, RESET);
    else if (status != 0) printf("%s(exit status %d)%s\n", DIM, status, RESET);
}

/* ---------- progress ---------- */

static char progress_path[PATH_MAX];
static int done[MAX_LESSONS];

static void load_progress(void) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(progress_path, sizeof progress_path, "%s/.shell-tutor/%s", home, advanced ? "progress-advanced" : "progress");
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
        printf("\n%s%sReview: %s — %s%s\n\n", BOLD, CYAN, l->section, l->title, RESET);
        return;
    }
    printf("\n%s%s%d/%d: %s — %s%s\n\n", BOLD, CYAN, i + 1, LESSON_COUNT, l->section, l->title, RESET);
    printf("%s\n\n", l->explain);
}

static void print_task(const Lesson *l) {
    printf("%s%sTask:%s %s\n", BOLD, YELLOW, RESET, l->task);
}

static void print_prompt_help(int kind) {
    if (kind == RUN)
        printf("%sType a command, or: hint, idk (show the answer), %sskip, list, quit%s\n", DIM, advanced ? "reset (fresh files), " : "", RESET);
    else
        printf("%sType a letter, or: hint, idk (show the answer), skip, list, quit%s\n", DIM, RESET);
}

/* Reads a raw line without trimming (here-document bodies keep their indentation). */
static char *read_raw_line(const char *prompt, char *buf, size_t size) {
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, (int)size, stdin)) { putchar('\n'); return NULL; }
    buf[strcspn(buf, "\n")] = '\0';
    return buf;
}

/*
 * Reads one command, which may span lines: a line ending in \ continues on
 * the next, and a here-document (<<WORD, <<'WORD' or <<-WORD) runs until
 * WORD alone on a line. The prompt for the extra lines is "> ", like zsh's.
 */
static char *read_command(char *buf, size_t size) {
    char line[2048];
    char *first = read_line("$ ", line, sizeof line);
    if (!first) return NULL;
    snprintf(buf, size, "%s", first);
    char terminator[64] = "";
    const char *h = strstr(buf, "<<");
    if (h) {
        h += 2;
        if (*h == '-') h++;
        while (*h == ' ') h++;
        if (*h == '\'' || *h == '"') h++;
        size_t n = 0;
        while ((isalnum((unsigned char)h[n]) || h[n] == '_') && n < sizeof terminator - 1) { terminator[n] = h[n]; n++; }
        terminator[n] = '\0';
    }
    for (;;) {
        size_t len = strlen(buf);
        int continued = len > 0 && buf[len - 1] == '\\';
        if (!continued && !*terminator) break;
        char *more = read_raw_line("> ", line, sizeof line);
        if (!more) return NULL;
        if (len + strlen(more) + 2 >= size) { printf("That command is too long.\n"); return buf; }
        buf[len] = '\n';
        strcpy(buf + len + 1, more);
        if (*terminator && strcmp(trim(more), terminator) == 0) terminator[0] = '\0';
    }
    return buf;
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
    if (strcmp(input, "reset") == 0) { reset_work_dir(); printf("Fresh copy of the example files.\n"); return 2; }
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

/* After a failed check: run the lesson's diagnosis, print what it says. */
static void explain_failure(const Lesson *l, const char *input) {
    if (!l->diagnose) return;
    run_shell(l->diagnose, work_dir, diag_file, input);
    FILE *f = fopen(diag_file, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) printf("%s  %s%s", YELLOW, line, RESET);
    fclose(f);
}

static int run_lesson(int i, int review) {
    const Lesson *l = &LESSONS[i];
    print_header(i, review);
    reset_work_dir();
    print_task(l);
    print_prompt_help(RUN);
    char buf[8192];
    int saw_answer = 0;   /* a pass after seeing the answer doesn't count; the lesson returns later */
    int tries = 0;
    for (;;) {
        char *input = read_command(buf, sizeof buf);
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
        if (access(work_dir, F_OK) != 0) {
            if (access(scratch_root, F_OK) != 0)
                printf("%sThe tutor's whole scratch folder (%s) was deleted while the tutor was running.\n"
                       "That was not this tutor: another terminal window, or a cleanup of the temporary folders.\n"
                       "Made a fresh one.%s\n", YELLOW, scratch_root, RESET);
            else
                printf("%sThe scratch folder (%s) was deleted between two of your commands.\n"
                       "Nothing typed here did that, so it came from outside: another terminal window,\n"
                       "the Finder, or a cleanup of the temporary folders. Made a fresh one.%s\n", YELLOW, work_dir, RESET);
            if (access(scratch_root, F_OK) != 0) mkdir(scratch_root, 0700);
            reset_work_dir();
        }
        int status = run_in_terminal(input, work_dir, out_file, input);
        show_output(status);
        if (access(work_dir, F_OK) != 0) {
            printf("%sThat command deleted the scratch folder itself, the folder you were standing in.\n"
                   "Made a fresh one for the next try.%s\n", YELLOW, RESET);
            reset_work_dir();
        }
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
        printf("%sNot quite.%s\n", RED, RESET);
        explain_failure(l, input);
        if (review && ++tries >= (advanced ? REVIEW_TRIES_ADVANCED : REVIEW_TRIES)) {
            printf("One way:  %s\nBack into the lessons it goes.\n", l->answer);
            forget(i);
            return NEXT;
        }
        if (advanced) {
            /* Files stay as they are between tries: write the script, then run it. */
            printf("Try again, or type hint. The files are as you left them; reset gives you fresh ones.\n");
        } else {
            printf("Try again, or type hint.\n");
            reset_work_dir();
        }
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

/* Lessons asked in the previous quick review; kept out of the next one when the pool allows. */
static int last_reviewed[REVIEW_SIZE], last_reviewed_n = 0;

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
 * At the end of a section, re-ask a few finished lessons from earlier
 * sections: walk back whole sections, as far as lesson 1 if needed, until
 * the pool holds REVIEW_POOL done lessons. Lessons asked in the previous
 * review are left out when enough others remain, so consecutive reviews
 * don't repeat themselves. No explanation is shown; two misses (or idk)
 * un-finish the lesson so it is taught again later.
 */
static int review_pass(int section_end) {
    int pool[LESSON_COUNT], n = 0;
    const char *section = LESSONS[section_end].section;
    for (int k = section_end - 1; k >= 0; k--) {
        const char *sec = LESSONS[k].section;
        if (strcmp(sec, section) == 0) continue;
        if (n >= REVIEW_POOL && strcmp(sec, LESSONS[k + 1].section) != 0) break;   /* section boundary, pool full */
        if (done[k]) pool[n++] = k;
    }
    if (n == 0) return NEXT;
    int fresh[LESSON_COUNT], f = 0;
    for (int j = 0; j < n; j++) {
        int repeat = 0;
        for (int d = 0; d < last_reviewed_n; d++) if (last_reviewed[d] == pool[j]) repeat = 1;
        if (!repeat) fresh[f++] = pool[j];
    }
    if (f >= REVIEW_SIZE) { memcpy(pool, fresh, f * sizeof *pool); n = f; }
    int count = n < REVIEW_SIZE ? n : REVIEW_SIZE;
    printf("\n%s%sQuick review%s — %d from earlier sections, no explanations this time.\n", BOLD, YELLOW, RESET, count);
    int result = review_lessons(pool, n, count);
    last_reviewed_n = count;
    memcpy(last_reviewed, pool, count * sizeof *pool);
    return result;
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
         "  shell-tutor --advanced [N | --list | --reset]\n"
         "                       the second tier: scripts, regex, sed, awk, PATH, permissions...\n"
         "\n"
         "At the prompt: hint, idk (show the answer), skip, list, goto N, quit.");
}

int main(int argc, char **argv) {
    use_color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    if (argc > 1 && strcmp(argv[1], "--advanced") == 0) {
        advanced = 1;
        LESSONS = ADVANCED;
        LESSON_COUNT = (int)(sizeof ADVANCED / sizeof ADVANCED[0]);
        argc--; argv++;
    }
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

    printf("%s%sshell-tutor%s%s — commands run in a scratch folder (%s), never in your files.\n",
           BOLD, CYAN, advanced ? " (advanced)" : "", RESET, work_dir);
    int any_done = 0;
    for (int k = 0; k < LESSON_COUNT; k++) any_done += done[k];
    if (!any_done && advanced) {
        printf("\nThe second tier. It assumes the first course: paths, pipes, redirection, grep,\n"
               "variables, wildcards. Two things are new at this prompt. A line ending in \\\n"
               "continues on the next line, and a here-document (cat > file <<'EOF') keeps\n"
               "reading until you type EOF alone on a line; that is how you will write scripts.\n"
               "Files you make stay put between tries within a lesson (type reset for fresh ones),\n"
               "so you can write a script with one command and run it with the next.\n");
    } else if (!any_done) {
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
            if (advanced)
                printf("Run   shell-tutor --advanced   again any time for another full review, or   shell-tutor --advanced --reset   to start from scratch.\n");
            else
                printf("Run   shell-tutor   again any time for another full review, or   shell-tutor --reset   to start from scratch.\n"
                       "Ready for more? There is a second tier:   shell-tutor --advanced\n");
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
    printf("%s%d of %d lessons done. Run shell-tutor%s again to continue.%s\n", DIM, finished, LESSON_COUNT, advanced ? " --advanced" : "", RESET);
    return 0;
}
