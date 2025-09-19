/**
	* Shell framework
	* course Operating Systems
	* Radboud University
	* v22.09.05

	Student names:
	- ...
	- ...
*/

/**
 * Hint: in most IDEs (Visual Studio Code, Qt Creator, neovim) you can:
 * - Control-click on a function name to go to the definition
 * - Ctrl-space to auto complete functions and variables
 */

// function/class definitions you are going to use
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>
#include <list>
#include <optional>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command {
  vector<string> parts = {};
};

struct Expression {
  vector<Command> commands;
  string inputFromFile;
  string outputToFile;
  bool background = false;
};

// Parses a string to form a vector of arguments. The separator is a space char (' ').
vector<string> split_string(const string& str, char delimiter = ' ') {
  vector<string> retval;
  for (size_t pos = 0; pos < str.length(); ) {
    // look for the next space
    size_t found = str.find(delimiter, pos);
    // if no space was found, this is the last word
    if (found == string::npos) {
      retval.push_back(str.substr(pos));
      break;
    }
    // filter out consequetive spaces
    if (found != pos)
      retval.push_back(str.substr(pos, found-pos));
    pos = found+1;
  }
  return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string>& args) {
  // build argument list
  const char** c_args = new const char*[args.size()+1];
  for (size_t i = 0; i < args.size(); ++i) {
    c_args[i] = args[i].c_str();
  }
  c_args[args.size()] = nullptr;
  // replace current process with new process as specified
  int rc = ::execvp(c_args[0], const_cast<char**>(c_args));
  // if we got this far, there must be an error
  int error = errno;
  // in case of failure, clean up memory (this won't overwrite errno normally, but let's be sure)
  delete[] c_args;
  errno = error;
  return rc;
}

// Executes a command with arguments. In case of failure, returns error code.
int execute_command(const Command& cmd) {
  auto& parts = cmd.parts;
  if (parts.size() == 0)
    return EINVAL;

  // execute external commands
  int retval = execvp(parts);
  return retval ? errno : 0;
}

void display_prompt() {
  char buffer[512];
  char* dir = getcwd(buffer, sizeof(buffer));
  if (dir) {
    cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
  }
  cout << "$ ";
  flush(cout);
}

string request_command_line(bool showPrompt) {
  if (showPrompt) {
    display_prompt();
  }
  string retval;
  getline(cin, retval);
  return retval;
}

// note: For such a simple shell, there is little need for a full-blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parse_command_line(string commandLine) {
  Expression expression;
  vector<string> commands = split_string(commandLine, '|');
  for (size_t i = 0; i < commands.size(); ++i) {
    string& line = commands[i];
    vector<string> args = split_string(line, ' ');
    if (i == commands.size() - 1 && args.size() > 1 && args[args.size()-1] == "&") {
      expression.background = true;
      args.resize(args.size()-1);
    }
    if (i == commands.size() - 1 && args.size() > 2 && args[args.size()-2] == ">") {
      expression.outputToFile = args[args.size()-1];
      args.resize(args.size()-2);
    }
    if (i == 0 && args.size() > 2 && args[args.size()-2] == "<") {
      expression.inputFromFile = args[args.size()-1];
      args.resize(args.size()-2);
    }
    expression.commands.push_back({args});
  }
  return expression;
}

int execute_expression(Expression& expression) {
  // Check for empty expression
  if (expression.commands.size() == 0)
    return EINVAL;

  // Handle intern commands (like 'cd' and 'exit')
  if(expression.commands[0].parts[0] == "exit")
    exit(0);

  if(expression.commands[0].parts[0] == "cd")
  {
    if(expression.commands[0].parts.size() != 2)
    {
      return EINVAL;
    }
    const char* path = expression.commands[0].parts[1].c_str();
    if(chdir(path) != 0)
    {
      return EINVAL;
    }
    return 0;
  }


  // External commands, executed with fork():
  // Loop over all commandos, and connect the output and input of the forked processes
  // create pipes
  size_t command_length = expression.commands.size();
  vector<pair<int,int>> pipes;
  vector<int> pids;

  for (size_t i = 0; i < command_length-1; ++i) {
    int x[2];
    if (pipe(x) == -1) {
        perror("pipe");
        exit(1);
    }
    pipes.emplace_back(x[0], x[1]);
  }

  for (size_t i = 0; i < command_length; ++i) {
    // external commands.
    pid_t pid = fork();
    if (pid == 0) {
      // set input file if its the first command and input file is given
      if (i == 0 && expression.inputFromFile != "") {
        int input = open(expression.inputFromFile.c_str(), O_RDONLY);
        if (input == -1) {
            return ENOENT;
        }
        dup2(input, STDIN_FILENO);
        close(input);
      }

      // set output file if its the last command and output file is given
      if (i == command_length - 1 && expression.outputToFile != "") {
        int output = open(expression.outputToFile.c_str(), O_WRONLY);
        if (output == -1) {
            return ENOENT;
        }
        dup2(output, STDOUT_FILENO);
        close(output);
      }

      // bind input if its not the first command
      if (i > 0) {
          dup2(pipes[i-1].first, STDIN_FILENO);
      }
      // bind output if its not the last command
      if (i < command_length-1) {
          dup2(pipes[i].second, STDOUT_FILENO);
      }

      for (size_t j = 0; j < command_length-1; ++j) {
          close(pipes[j].first);
          close(pipes[j].second);
      }

      execute_command(expression.commands[i]);
    }
    pids.push_back(pid);
  }

  for (size_t i = 0; i < command_length-1; ++i) {
    close(pipes[i].first);
    close(pipes[i].second);
  }

  if (!expression.background) {
    for (int pid : pids) {
      waitpid(pid, nullptr, 0);
    }
  }

  return 0;
}

// framework for executing "date | tail -c 5" using raw commands
// two processes are created, and connected to each other
int step1(bool showPrompt) {
  // create communication channel shared between the two processes
  // ...
  int p[2];
  pipe(p);
  pid_t child1 = fork();
  if (child1 == 0) {
    // redirect standard output (STDOUT_FILENO) to the input of the shared communication channel
    // free non used resources (why?)
    dup2(p[1], STDOUT_FILENO);
    close(p[0]);
    close(p[1]);
    Command cmd = {{string("date")}};
    execute_command(cmd);
    // display nice warning that the executable could not be found
    perror("an error has occured");
    abort(); // if the executable is not found, we should abort. (why?)
  }

  pid_t child2 = fork();
  if (child2 == 0) {
    // redirect the output of the shared communication channel to the standard input (STDIN_FILENO).
    // free non used resources (why?)
    dup2(p[0], STDIN_FILENO);
    close(p[0]);
    close(p[1]);
    Command cmd = {{string("tail"), string("-c"), string("5")}};
    execute_command(cmd);
    perror("an error has occured");
    abort(); // if the executable is not found, we should abort. (why?)
  }

  // free non used resources (why?)
  close(p[0]);
  close(p[1]);
  // wait on child processes to finish (why both?)
  waitpid(child1, nullptr, 0);
  waitpid(child2, nullptr, 0);
  return 0;
}

int shell(bool showPrompt) {
  //* <- remove one '/' in front of the other '/' to switch from the normal code to step1 code
  while (cin.good()) {
    string commandLine = request_command_line(showPrompt);
    if(!cin.good())
    {
      cout << endl;
      break;
    }
    Expression expression = parse_command_line(commandLine);
    int rc = execute_expression(expression);
    if (rc != 0)
      cerr << strerror(rc) << endl;
  }
  return 0;
  /*/
  return step1(showPrompt);
  //*/
}
