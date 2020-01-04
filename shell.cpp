#include<sys/wait.h>
#include<sys/types.h>
#include<unistd.h>
#include<pwd.h>
#include<cstdlib>
#include<iostream>
#include<algorithm>
#include<unordered_map>
#include<unordered_set>
#include<string>
#include<vector>
#include<memory>
#include<deque>
#include<initializer_list>
#include<utility>
using namespace std::string_literals;
using Args=std::vector<const char*>;
using Args_ptr=const char*const*;
using Tokens=std::deque<std::string>;
bool find_str(const char *s,char c){
	for(auto p=s;*p!='\0';p++)
		if(*p==c)
			return true;
	return false;
}
int tokenize(const std::string &s,Tokens &tokens){
	static auto space=" \n\t";
	static auto delimiter=" \n\t|=\"\'";
	tokens.clear();
	std::string t;
	enum class Context{
		common,
		double_quoted,
		single_quoted,
		backslash,
	};
	Context context=Context::common;
	for(auto x:s){
		switch(context){
		case Context::backslash:
			t+=x;
			context=Context::common;
			break;
		case Context::common:
			if(!t.empty()&&find_str(delimiter,x)){
				tokens.push_back(t);
				t.clear();
			}
			if(find_str(space,x))
				break;
			switch(x){
			case '\\':
				context=Context::backslash;
				break;
			case '\"':
				context=Context::double_quoted;
				break;
			case '\'':
				context=Context::single_quoted;
				break;
			default:
				t+=x;
			}
			break;
		case Context::double_quoted:
			switch(x){
			case '\\':
				context=Context::backslash;
				break;
			case '\"':
				context=Context::common;
				break;
			default:
				t+=x;
			}
			break;
		case Context::single_quoted:
			if(x=='\'')
				context=Context::common;
			else
				t+=x;
			break;
		}
	}
	if(context!=Context::common)
		return -1;
	if(!t.empty())
		tokens.push_back(t);
	return 0;
}
int split(const Tokens &tokens,std::vector<Args> &commands){
		Tokens::const_iterator first=std::begin(tokens);
		commands.clear();
		Args t;
		for(auto it=std::begin(tokens);;it++){
			if(it==std::end(tokens)||*it=="|"s){
				if(first==it)
					return -1;
				t.push_back(nullptr);
				commands.push_back(std::move(t));
				t.clear();
				if(it==std::end(tokens))
					break;
				first=it;
				first++;
			}else{
				t.push_back(it->c_str());
			}
		}
	return 0;
}
class Shell;
using Command_type=int(Shell::*)(Args_ptr);
class Shell{
	std::unordered_map<std::string,std::string> env_var;
	const std::unordered_map<std::string,Command_type> builtin_command;

	Command_type find_builtin(const char *s){
		auto it=builtin_command.find(s);
		if(it!=std::end(builtin_command))
			return it->second;
		return nullptr;
	}
	int execute(Args_ptr args,bool f){
		auto cmd=find_builtin(args[0]);
		bool child=false;
		if(cmd!=nullptr){
			return (this->*cmd)(args);
		}else{
			if(f){
				int pid=fork();
				if(pid==-1){
					perror("shell");
					return -1;
				}else if(pid==0){
					child=true;
				}else{
					int status;
					waitpid(pid,&status,0);
					if(WIFEXITED(status))
						return WEXITSTATUS(status);
					return -1;
				}
			}
			if(execvp(args[0],const_cast<char*const*>(args))!=0){
				perror("shell");
				if(child)
					exit(-1);
				return -1;
			}
		}
		if(child)
			exit(0);
		return 0;
	}

	int c_builtin(Args_ptr args){
		if(args[1]==nullptr)
			return 0;
		auto cmd=find_builtin(args[1]);
		if(cmd==nullptr){
			std::cerr<<"Command not found."<<std::endl;
			return -1;
		}
		return (this->*cmd)(args+1);
	}
	int c_command(Args_ptr args){
		if(args[1]==nullptr)
			return 0;
		return execute(args+1,true);
	}
	int c_echo(Args_ptr args){
		if(args[1]==nullptr)
			return 0;
		auto it=args+1;
		std::cout<<*it;
		for(it++;*it!=nullptr;it++)
			std::cout<<" "<<*it;
		std::cout<<std::endl;
		return 0;
	}
	int c_exit(Args_ptr args){
		if(isatty(STDOUT_FILENO))
			std::cout<<"exit"<<std::endl;
		exit(0);
		return 0;
	}
	int c_cd(Args_ptr args){
		if(args[1]==nullptr)
			return 0;
		if(chdir(args[1])!=0)
			perror("shell");
		return 0;
	}
public:
	Shell(const Args &args):
		builtin_command({
				{"command"s,&Shell::c_command},
				{"echo"s,&Shell::c_echo},
				{"exit"s,&Shell::c_exit},
				{"cd"s,&Shell::c_cd},
				})
	{
		signal(SIGTTIN,SIG_IGN);
		signal(SIGTTOU,SIG_IGN);
	}
	int interpret(const std::string &s){
		Tokens tokens;
		tokenize(s,tokens);
		if(tokens.size()<1)
			return 0;
		std::vector<Args> commands;
		if(split(tokens,commands)!=0){
			std::cerr<<"Syntax error"<<std::endl;
			return -1;
		}
		auto n=commands.size();
		if(n<1)
			return 0;
		if(n==1){
			auto cmd=find_builtin(commands[0][0]);
			if(cmd!=nullptr){
				(this->*cmd)(commands[0].data());
				return 0;
			}
		}
		pid_t pgid=-1;
		int fd_in,fd_out[2];
		fd_out[0]=STDIN_FILENO;
		for(std::size_t i=0;i<n;i++){
			fd_in=fd_out[0];
			if(i+1==n){
				fd_out[1]=STDOUT_FILENO;
			}else{
				pipe(fd_out);
			}
			pid_t pid=fork();
			if(pid==-1){
				perror("shell");
				exit(-1);
			}else{
				pid_t child_pid;
				if(pid==0)
					child_pid=getpid();
				else
					child_pid=pid;
				if(pgid==-1){
					setpgid(child_pid,0);
					pgid=child_pid;
					tcsetpgrp(STDIN_FILENO,pgid);
				}else{
					setpgid(child_pid,pgid);
				}
				if(pid==0){
					if(fd_in!=STDIN_FILENO){
						dup2(fd_in,0);
						close(fd_in);
					}
					if(fd_out[1]!=STDOUT_FILENO){
						dup2(fd_out[1],1);
						close(fd_out[1]);
					}
					exit(execute(commands[i].data(),false));
				}else{
					if(fd_in!=STDIN_FILENO)
						close(fd_in);
					if(fd_out[1]!=STDOUT_FILENO)
						close(fd_out[1]);
				}
			}
		}
		int status;
		for(auto live=n;live;live--)
			wait(&status);
		tcsetpgrp(STDIN_FILENO,getpgrp());
		return 0;
	}
};
class Input{
};
int main(){
	std::string s;
	Shell shell({});
	char path_buf[1024],hostname_buf[1024];
	auto pwuid=getpwuid(getuid());
	if(pwuid==NULL){
		perror("shell");
		exit(-1);
	}
	bool stdout_term=isatty(STDOUT_FILENO);
	while(1){
		if(getcwd(path_buf,sizeof(path_buf))==NULL){
			perror("shell");
			exit(1);
		}
		if(gethostname(hostname_buf,sizeof(hostname_buf))!=0){
			perror("shell");
			exit(1);
		}
		if(stdout_term)
			std::cout<<pwuid->pw_name<<"@"<<hostname_buf<<":"<<path_buf<<">"<<std::flush;
		getline(std::cin,s);
		if(std::cin.eof())
			break;
		shell.interpret(s);
	}
}
