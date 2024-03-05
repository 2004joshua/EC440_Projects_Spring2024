#include "myshell_parser.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h> 

int main(int argc, char* argv[]){

  int pipe_in = 0; //input file descriptor
  bool show = true; 

  for(int i = 0; i < argc; i++){
	if(strcmp(argv[i], "-n") == 0){
		show = false;
		break; 
	}
  }

  char input[MAX_LINE_LENGTH + 1] = {0}; 

  while(1){ //the whole shell is one big while loop with ctrl + d as the exit condition
    
    if(show){
	printf("my_shell$"); 
    }

    if(fgets(input, MAX_LINE_LENGTH, stdin) == NULL){
		
	    printf("\n"); 

	    if(input != NULL){
		memset(input, 0, sizeof(input)); 
	    }

	    return EXIT_SUCCESS; 
    }

    if(input[0] == '\n'){
	continue;
    }
    struct pipeline *pipeline = pipeline_build(input);
    struct pipeline_command *current = pipeline->commands;

    while(current != NULL){ //traversing the linked list 
	
	if(strcmp(current->command_args[0], "cd") == 0){
                   
		if(current->command_args[1] != NULL){
                       
		       	if(chdir(current->command_args[1]) != 0){//chdir allows to change directories
                                        fprintf(stderr, "ERROR: No directory found!\n");
                        }
                }
                
		else{
                        const char* home = getenv("HOME");
			
			if(home){
                                chdir(home);
                        }
                        else{
                                fprintf(stderr, "ERROR: cd: home not set!\n");
                        }
                }

                        current = current->next;
                        continue;
                }

      int fd[2];

      if(current->next != NULL){
		pipe(fd); //pipes for all but the very last command 
      }

      pid_t pid = fork(); //create new process for current command
      
      if(pid == -1){ //error
		fprintf(stderr,"ERROR: Failure to make a child process!\n");
		pipeline_free(pipeline);
		exit(EXIT_FAILURE);
      }
      
      else if(pid == 0){ //child process

	if(pipe_in != 0){
		dup2(pipe_in, STDIN_FILENO); //redirects input from the pipe before
		close(pipe_in); 
	}

	if(current->next != NULL){//if another command exists this will output to the next pipe
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO); 
		close(fd[1]); 
	}
	
	if(current->redirect_in_path != NULL){//redirect_in_path handling if file exists

	  	int fd_in = open(current->redirect_in_path, O_RDONLY);

	  	if(fd_in == -1){

	    		fprintf(stderr, "ERROR: Failure to open redirect_in_path!\n");
	    		pipeline_free(pipeline);
	    		exit(EXIT_FAILURE);
	  	}

	  	if(dup2(fd_in, STDIN_FILENO) == -1){
	    		fprintf(stderr,"ERROR: Failure to use dup2 in redirect_in_path!\n");
	    		pipeline_free(pipeline);
	    		exit(EXIT_FAILURE); 
	  	}

	  	close(fd_in); 
	}

	if(current->redirect_out_path != NULL){//redirect_out_path handling if file exists
	  	int fd_out = open(current->redirect_out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	  	if(fd_out == -1){
	    		fprintf(stderr, "ERROR: Failure to open redirect_out_path!\n");
	    		pipeline_free(pipeline);
	    		exit(EXIT_FAILURE); 
	 	}

	 	 if(dup2(fd_out, STDOUT_FILENO) == -1){
	    		fprintf(stderr, "ERROR: Failure to dup2 redirect_out_path!\n");
	    		pipeline_free(pipeline);
	    		exit(EXIT_FAILURE); 
	  	}

	 	 close(fd_out); 
	}
	
	if(execvp(current->command_args[0], current->command_args) == -1){//executing commands in node, if it doesnt happen it prompts error
		fprintf(stderr, "ERROR: try: no file or directory\n"); 
		pipeline_free(pipeline); 
		exit(EXIT_FAILURE); 
	}	
      }//end of child
	else{ //beginning of parent

		if(current->next != NULL){//handles next command if there is one
	    		close(fd[1]); //closes writing

	    		if(pipe_in != 0){
		      		close(pipe_in); //closes previous pipe
	    		}

	    		pipe_in = fd[0]; 
	  	}

	  	if(!pipeline->is_background){ 
			//waits for the child to finish if its not a background job
	    		wait(NULL); 
	  	}
	}//end of parent

      		current = current->next; //move to the next node/command 
  } //end of traversing

    		memset(input, 0, sizeof(input));
	       pipeline_free(pipeline); 	
  } //end of while(1)
}//end of main
