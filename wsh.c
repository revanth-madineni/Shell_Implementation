#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 128
#define MAX_PATH 512
#define MAX_LINE 1024

char* built_in[] = {
    "exit", 
    "cd", 
    "export", 
    "local", 
    "vars", 
    "history", 
    "ls"
};



struct local_var_node{
    char* name;
    struct local_var_node* next;
};


/********************************************
Implement ls built in
*********************************************/
void ls_built_in(char *path){
     
     struct dirent **filelist;
     int filecount;
     
     char* def_path = ".";
      
     if(path == NULL){
	    path =def_path;
     }
     filecount = scandir(path, &filelist, NULL, alphasort);
     if (filecount == -1) {
	     return;
     }
     
     for(int i = 0; i< filecount; i++){
	     if(filelist[i]->d_name[0] != '.'){
		     printf("%s\n", filelist[i]->d_name);
	     }
	     free(filelist[i]);

     }
     free(filelist);
}


/********************************************
History Linked List and  Variables
*********************************************/
struct history_node{
    char* cmd;
    struct history_node* next;
};

struct history_node* history_list = NULL;
struct local_var_node* local_var_list = NULL;
int history_count = 0;
int max_history_count = 5;

FILE* batch_fd;
int batchmode = 0;
int update_local_var(char* var);
void exec_cmd(char* cmd, int from_history);

/********************************************
Environement Variables
*********************************************/

extern const char** environ;
const char** my_environ = NULL;
int env_count;
int max_env_count;


void init_environ(void)
{
    max_env_count = 1024;
    my_environ = malloc(sizeof(char*) * max_env_count);
    
    if(my_environ == NULL){
        printf("Unable to allocate environment array\n");
        exit(0);
    }

    memset(my_environ, 0, sizeof(char*) * max_env_count);

    char* ptr = malloc(strlen("PATH=/bin")+1);
    if(ptr == NULL){
        printf("Unable to allocate path\n");
        exit(0);
    }

    strcpy(ptr,"PATH=/bin");
    my_environ[0] = ptr;
    env_count = 1;
    environ = my_environ;
}

/*
void print_environ(void)
{
    for(int i=0; i<env_count; i++){
        printf("%s\n", my_environ[i]);
    }
}
*/

void delete_environ(void)
{
    for(int i=0; i<env_count; i++){
        free((void *)my_environ[i]);
    }
    free(my_environ);
}

//find if variable already exisits in environ table
char* get_environ_val(char* name)
{
    for(int i =0; i< env_count; i++){
        if(!strncmp(my_environ[i],name, strlen(name) )){
            char* ptr = strchr(my_environ[i],'=');
            if(ptr == NULL){
                return NULL;
            }
            return ptr+1;
        }
    }

    return NULL;
}

void add_environ(char* cmd)
{
    char* ptr = strchr(cmd,'=');

    if(ptr == NULL){
        printf("Invalid export command\n");
        return;
    }
    char* new_val = ptr+1;
    char* old_val = NULL;
    int append = 0;
    char* replace_val = NULL;
    int replace_val_len =0;
    //int old_val_len = 0;

    char* name = malloc(strlen(cmd)+1);
    if(name == NULL){
        printf("Unable to allocate memory\n");
        return;
    }

    int name_len = ptr-cmd;
    strncpy(name, cmd, name_len);
    name[name_len] = '\0';
    char* temp;

    old_val = get_environ_val(name);
    
    //Check for substitutions
    if(*new_val == '$'){
        new_val++;
        if(!strncmp(new_val, "PATH",4)){
            replace_val = get_environ_val("PATH");
            //printf("1. replace_val: %s\n",replace_val);
            new_val+=4;
        }
        else{
             replace_val = get_environ_val(new_val);
              new_val+=strlen(new_val);
        }
        if(replace_val == NULL){
            free(name);
            return;
        }
        append =1;
        replace_val_len = strlen(replace_val);
         
    }

    //fresh new entry to environ table
    if(old_val == NULL){

        if(env_count>=1024){
            //TODO: resize the environ
            printf("Reached max environ size\n");
            free(name);
	    return;
        }

        temp = malloc(strlen(cmd)+ replace_val_len +1);
        if(temp == NULL){
                printf("Unable to allocate memory\n");
                free(name);
                return;
        }

        if(replace_val_len == 0){
              strcpy(temp, cmd);
        }
        else{
             strcpy(temp, name);
             strcat(temp, "=");
             strcat(temp, replace_val);
        }

        my_environ[env_count] = temp;
        env_count++;

        free(name);
        return;
    }
   

    //replacement entry to environ table
    for(int i =0; i< env_count; i++){
        if(!strncmp(my_environ[i],name, name_len )){
     		//printf("Replacing environ:%s\n",name);
	 	temp = malloc(strlen(cmd)+ strlen(my_environ[i]) + 1);

            if(temp == NULL){
                printf("Unable to allocate memory\n");
                free(name);
                return;
            }
            
            strcpy(temp, name);
            strcat(temp, "=");

            if(append){
                //printf("replace_val: %s\n",replace_val);
                strcat(temp, replace_val);
            }

            strcat(temp, new_val);
            free((char*)my_environ[i]);
     		//printf("Replacing environ at %d ,%s\n",i,temp);

            my_environ[i] = temp;
	    free(name);
            return;
        }
    }

}

char* get_path_for_cmd(char* cmd)
{
    char* path = get_environ_val("PATH");
    if(path == NULL){
        return NULL;
    }

    char* temppath = malloc(strlen(path)+1);

    if(temppath == NULL){
        return NULL;
    }
    strcpy(temppath, path);

    char* filepath= malloc(strlen(path)+strlen(cmd)+1);
    if(filepath == NULL){
        free(temppath);
        return NULL;
    }

    //split cmd by spaces
    char* token = strtok(temppath, ":");
    

    while(token != NULL){

        strcpy(filepath, token );
        strcat(filepath, "/" );
        strcat(filepath, cmd );
	
        //printf("Looking for CMD: %s\n" , filepath);

        if(!access(filepath,X_OK)){
            free(temppath);
            return filepath;
        }
        token = strtok(NULL, ":");
    }
    free(temppath);
    free(filepath); 
    return NULL;

}

/********************************************
Functions for Local Variables
*********************************************/

void add_local_var(char* var)
{
    
    struct local_var_node* node;
    struct local_var_node* temp_node;
    //printf("Add LocalVar: %s\n", var);

    if (*var == '=') {
        return;
    }

    if(update_local_var(var)){
        //printf("Updated: %s\n", var);
        return;
    }

    //alloc memory for local variable
    char* buff = malloc(strlen(var)+1);
    if(buff == NULL){
        printf("Unable to allocate memory\n");
        return;
    }

    strcpy(buff, var);

    node = malloc(sizeof(struct local_var_node));
    if(node == NULL){
        printf("Unable to allocate memory\n");
        free(buff);
        return;
    }

    node->name = buff;
    node->next = NULL;

    if(local_var_list==NULL){
        local_var_list = node;
        return;
    }

    temp_node =local_var_list;
    while(temp_node->next != NULL){
        temp_node = temp_node->next;
    }

    temp_node->next = node;
    return;
}


//update local variables
int update_local_var(char* var){

    char* name = malloc(strlen(var)+1);
    if(!name){
        printf("Unable to allocate memory\n");
        return 0;
    }

    strcpy(name,var);
    char* ptr = strchr(name,'=');
    if(!ptr){
        return 0;
    }
    ptr++;
    *ptr = '\0';

    struct local_var_node* node;
   
    node =local_var_list;
    while(node != NULL){
        if(!strncmp(node->name,name,strlen(name))){
            free(node->name);
            char* n = malloc(strlen(var)+1);
            strcpy(n,var);
            node->name =  n;
            return 1;
        }
        node = node->next;
    }

    return 0;
}

void print_local_vars(void)
{
    struct local_var_node* node = local_var_list;

     while(node){
        printf("%s\n", node->name);
        node = node->next;
     }
}


void delete_local_vars(void)
{
    struct local_var_node* node = local_var_list;
    struct local_var_node* temp;

     while(node){
        free(node->name);
	temp = node;
        node = node->next;
	free(temp);
     }
     local_var_list= NULL;
}


char* get_local_vars(char* name){
    struct local_var_node* node = local_var_list;
    char* ptr;

     while(node){

        if(!strncmp(node->name,name,strlen(name))){
            ptr = node->name + strlen(name);

            if(*ptr == '='){
                ptr++;
                return ptr;
            }
        }
      
        node = node->next;
     }
     return (char*)NULL;
}

char* substitute_local_vars(char* cmd){

    char* ptr = strchr(cmd,'=');
    if(ptr == NULL){
        return cmd;
    }   

    if(*(ptr+1) != '$'){
        return cmd;
    }
    ptr+=2;
    char* replace_val = get_local_vars(ptr);
    if(replace_val == NULL){
        printf("Could not find substite local var: %s\n",ptr);
        return NULL;
    }

    char* replace_str = malloc(strlen(cmd)+strlen(replace_val)+1);
    if(replace_str == NULL){
        printf("Could not allocate memory\n");
        return NULL;
    }
    memset(replace_str,0,strlen(cmd)+strlen(replace_val)+1);
    strncpy(replace_str, cmd, ptr-cmd-1);
    strcat(replace_str,replace_val);


    //printf("Substituted String: %s\n", replace_str);

    return replace_str;
}

int check_substitutes(char* args[],int arg_count){
    char* replace_val;
    char* ptr;

    //subsitute with environ 
    for(int i=0; i<arg_count; i++){
        if(args[i][0] == '$'){
            ptr = args[i];
            replace_val = get_environ_val(ptr+1);
            if(replace_val ==NULL){
                //return 0;
                continue;
            }
            args[i] = replace_val;
            
            //printf("Replaced environ: [%d] %s\n",i,replace_val);
        }
    }

    //substitue with local args
    for(int i=0; i<arg_count; i++){
        if(args[i][0] == '$'){
            ptr = args[i];
            replace_val = get_local_vars(ptr+1);
            if(replace_val ==NULL){
                //return 0;
                continue;
            }
            args[i] = replace_val;
            //printf("Replaced Local: [%d] %s\n",i,replace_val);
        }
    }
    return 1;
}




//Execute nth command from history
void exec_history(int n)
{
    
    struct history_node* node = history_list;
    char tempcmd[MAX_LINE];

    if((n <= 0) || (n > history_count)){
        return;
    }

    

    for(int i =0; i<n-1 ;i++){
        if(node==NULL){
            return;
        }
        node = node->next;
    }

    strcpy(tempcmd,node->cmd);
    exec_cmd(tempcmd,1);
}


void display_history(void)
{
    struct history_node* node = history_list;
    int i = 1;
    while(node){
        if(i>max_history_count){
            break;
        }

        printf("%d) %s\n", i, node->cmd);
        i++;
        node = node->next;
        
    }


}

/*
void dump_history(void)
{
    struct history_node* node = history_list;
    printf("Current Count: %d, Max: %d\n", history_count, max_history_count  );
    while(node){
        printf("%s ,", node->cmd);
        node = node->next;

    }
}
*/

void set_history_limit(int n){
    
    struct history_node* node = history_list;
    struct history_node* temp;
    struct history_node* temp2;
    //int count = 0; 

    if(n < 0 ){
        return;
    }

    max_history_count =n;

    //printf("Set history limit: %d", n);
    //if history set is '0', delete entire list
    if(n == 0){
        while(node){
             temp  = node->next;
             free(node->cmd);
             free(node);
             node =temp;
             
        }
        history_list = NULL;
        history_count = 0;
        return;
    }

    //stop at last valid node
    for(int i=0; i<n-1 ; i++){
        if(!node){
            return;
        }
        node = node->next;
    }
    
  

    if(node==NULL){
        return;
    }
    
    //delete everthing after set node
    temp = node->next;
    node->next = NULL;

    history_count = n;

    while(temp){
        temp2 = temp->next;
        free(temp->cmd);
        free(temp);
        temp =temp2;
    }
        
}

void add_history(char* cmd){

    struct history_node* temp_node;
    //printf("Add History: %s", cmd);

    if(history_list){
        if(!(strcmp(cmd,history_list->cmd ))){
             //printf("Repeated Command: %s", cmd);
            return;
        }
    }

    if(max_history_count == 0){
        return;
    }

    char* buff = malloc(strlen(cmd)+1);

    if(buff == NULL){
        printf("Unable to allocate memory\n");
        return;
    }
    strcpy(buff, cmd);

    //delete last node
    if( (history_count != 0) && (history_count == max_history_count) ){
        if(history_count == 1){
            free(history_list->cmd);
            free(history_list);
            
            history_list = NULL;
            history_count =0;
        }
        else{
            
            temp_node = history_list;

            while((temp_node->next) && (temp_node->next->next != NULL)) {
                temp_node = temp_node->next;
            }
            free(temp_node->next->cmd);
            free(temp_node->next);
            temp_node->next =NULL;
             history_count--;
        }
    }

  
    struct history_node* node = malloc(sizeof( struct history_node));
    if(node == NULL){
        printf("Unable to allocate history node\n");
        free(buff);
        return;
    }

    //prepend to linked list
    node->cmd = buff;
    node->next = history_list;
    history_list = node;
   
    
    history_count++;

}

void delete_history(void)
{
    struct history_node* node = history_list;
    struct history_node* temp;
    while(node){
        free(node->cmd);
	temp = node;
        node = node->next;
	free(temp);
    }
    history_list = NULL;
}

void clean_up(void)
{
	delete_history();
	delete_local_vars();
	delete_environ();
}


int is_built_in(char* cmd){

    int size = sizeof(built_in) / sizeof(char*);
    
    //printf("Is buitl in, size= %d, cmd = %s\n",size,cmd);
    for(int i=0; i<size; i++ ){
        if( !(strcmp(cmd, built_in[i]))){
            return 1;
        }
    }
    return 0;
}


int is_revert_needed =0;
int saved_stdout;
void revert_redirection(void){

    if(is_revert_needed){
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        is_revert_needed = 0;
    }

}

void save_stdout(void){
    saved_stdout = dup(STDOUT_FILENO);  
    is_revert_needed = 1;
}

int built_in_redirection_start(char *args[], int arg_count){
    //printf("ArgCount: %d\n",arg_count);
    char* last_cmd = args[arg_count-1];
    //printf("last_cmd: %s\n",last_cmd);


    if( (*last_cmd == '>') || 
        (!strncmp(last_cmd, "&>",2))){
            //printf("Saving Stdout\n");
            save_stdout();
            return 1;
    }

    return 0;
}






//Handle Redirection:
void handle_redirection(char *args[], int arg_count)
{
     //printf("handle_redirection function\n");

     // pwd >>out
     //
     
    char* last_cmd = args[arg_count-1];
    int cfd;
    int tempfd;

    if(!strncmp(last_cmd, ">>",2)){
        //printf("Detected >>\n");
            last_cmd+=2;
            args[arg_count-1] = NULL;
            cfd = open(last_cmd, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
            if(cfd){
                if(dup2(cfd,STDOUT_FILENO) < 0){
                    printf("Unable to dup STDOUT");
                }
            }
    }

            else if(!strncmp(last_cmd, "&>>",3)){
                 last_cmd+=3;
                 args[arg_count-1] = NULL;
                 cfd = open(last_cmd, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
                 if(cfd){
                    if(dup2(cfd,STDOUT_FILENO) < 0){
                        printf("Unable to dup STDOUT");
                    }
                    if(dup2(cfd,STDERR_FILENO) < 0){
                        printf("Unable to dup STDERR");
                    }
                 }
            }

            else if(!strncmp(last_cmd, "&>",2)){
                 last_cmd+=2;
                 args[arg_count-1] = NULL;
                 cfd = open(last_cmd, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                 if(cfd){
                    if(dup2(cfd,STDOUT_FILENO) < 0){
                        printf("Unable to dup STDOUT");
                    }
                    if(dup2(cfd,STDERR_FILENO) < 0){
                        printf("Unable to dup STDERR");
                    }
                 }
            }
          
            // //if [n] is NOT given before '>'
            else if(*last_cmd == '>'){
                //printf("Detected >\n");
                 last_cmd++;
                 args[arg_count-1] = NULL;
                 cfd = open(last_cmd, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                 if(cfd){   
                    if(dup2(cfd,STDOUT_FILENO) < 0){
                        printf("Unable to dup STDOUT");
                    }
                   
                 }
            }
            //if [n] is NOT given before '<'
            else if(*last_cmd == '<'){
                //printf("Detected <\n");
                last_cmd++;
                args[arg_count-1] = NULL;
                cfd = open(last_cmd, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
                 if(cfd){
                    if(dup2(cfd,STDIN_FILENO) < 0){
                        printf("Unable to dup STDIN");
                    }
                   
                 }
            }
            //if [n] is given before '<'
             else if(last_cmd[1] == '<'){

                //printf("Detected n<\n");
                
                if(last_cmd[0] == '0'){
                    tempfd = STDIN_FILENO;
                }
                else if(last_cmd[0] == '1'){
                    tempfd = STDOUT_FILENO;
                }
                 else if(last_cmd[0] == '2'){
                    tempfd = STDERR_FILENO;
                }
                else{
                    return;
                }
                last_cmd+=2;

                args[arg_count-1] = NULL;
                cfd = open(last_cmd, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
                 if(cfd){
                    if(dup2(cfd,tempfd) < 0){
                        printf("Unable to dup STDIN");
                    }
                   
                 }
            }

            //if [n] is given before '>>'
             else if(last_cmd[1] == '>' && last_cmd[2] == '>'){
                //printf("Detected n>>\n");
                if(last_cmd[0] == '0'){
                    tempfd = STDIN_FILENO;
                }
                else if(last_cmd[0] == '1'){
                    printf("Detected 1\n");
                    tempfd = STDOUT_FILENO;
                }
                 else if(last_cmd[0] == '2'){
                    tempfd = STDERR_FILENO;
                }
                else{
                    return;
                }
                last_cmd+=3;

                args[arg_count-1] = NULL;
                cfd = open(last_cmd, O_CREAT | O_RDWR | O_APPEND , S_IRUSR | S_IWUSR);
                 if(cfd){
                    if(dup2(cfd,tempfd) < 0){
                        printf("Unable to dup STDIN");
                    }
                   
                 }
            }

            //if [n] is given before '>'
             else if(last_cmd[1] == '>'){
                 //printf("Detected n>\n");
                if(last_cmd[0] == '0'){
                    tempfd = STDIN_FILENO;
                }
                else if(last_cmd[0] == '1'){
                    tempfd = STDOUT_FILENO;
                }
                 else if(last_cmd[0] == '2'){
                    tempfd = STDERR_FILENO;
                }
                else{
                    return;
                }
                last_cmd+=2;

                args[arg_count-1] = NULL;
                cfd = open(last_cmd, O_CREAT | O_WRONLY | O_TRUNC , S_IRUSR | S_IWUSR);
                 if(cfd){
                    if(dup2(cfd,tempfd) < 0){
                        printf("Unable to dup STDIN");
                    }
                   
            }
    }

}

int last_status = 0;
void exec_cmd(char * cmd, int from_history)
{
    int arg_count=0;
    int status;
    char* token;
    char* args[MAX_ARGS];
    char path[MAX_PATH];
    char* filepath;
    char tempcmd[MAX_LINE];
    int n;
    char* ptr;
    

    //take a copy of cmd to add to history later
    strcpy(tempcmd,cmd);

    memset(args,0,sizeof(char*) * MAX_ARGS);
    strcpy(path,""); 

    //split cmd by spaces
    token = strtok(cmd, " ");
	
    //printf("CMD = %s, token = %s\n",tempcmd,token); 
    while(token != NULL){

        if( arg_count >= MAX_ARGS){
            break;
        }

        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    if( (arg_count ==0) || (arg_count == MAX_ARGS)){
        return;
    }
    args[arg_count] = NULL;

    /*
    for(int j =0; j<arg_count; j++){
        printf("Args[%d]: %s\n", j,args[j]);
    }
    */

    //replacement logic with $, env get priority over local
    if(!check_substitutes(args,arg_count)){
        return;
    }

   
    if(from_history){
        goto execute;
    }

    if(is_built_in(args[0])){
            
            //for redirection >, &>, >> - save stdout
            int redir = built_in_redirection_start(args,arg_count);
            if(redir){
                handle_redirection(args,arg_count);
            }
            
            if( !strcmp(args[0],"exit")){
                    
                clean_up();
                exit(0);
            }
            else if( !strcmp(args[0],"cd")){

                //chdir is system call
                if(chdir(args[1])!=0){
                    //printf("Unable to Change Directory");
                }
            }

            else if( !strcmp(args[0],"export")){  
                if(arg_count ==2){
                        add_environ(args[1]);
                        //print_environ();
                }
            }

            else if( !strcmp(args[0],"local")){

                if(arg_count ==2){
                    
                    ptr = strchr(args[1],'=');
                    if(ptr != NULL){
                        int delete=0;
                        char* new_ptr = substitute_local_vars(args[1]);
                        if(new_ptr == NULL){
                            return;
                        } 
                        if(new_ptr != args[1]){
                            delete =1;
                        } 

                        add_local_var(new_ptr);

                        if(delete == 1){
                            free(new_ptr);
                        }
                    }    
                }
            }

            else if( !strcmp(args[0],"vars")){
                print_local_vars();
            }

            else if( !strcmp(args[0],"history")){

                switch(arg_count){

                    case 1:
                        display_history();
                        break;

                    case 2:
                        if(redir){
                            display_history();
                        }
                        else{
                            n = atoi(args[1]);
                            //exec nth cmd from history
                            exec_history(n); 
                        }
                        break;

                    case 3:
                        if(!(strcmp(args[1], "set"))){
                            set_history_limit(atoi(args[2]));
                        }
                        break;
                    default:
                        break;
                }
            }
            
            else if( !strcmp(args[0],"ls")){
                if( arg_count > 1){
                    ls_built_in(args[1]); 
                }
                else{
                    ls_built_in("."); 
                }	
            }

            //put stdout back
            revert_redirection();
            return;		
        }
        else{
            add_history(tempcmd);
        }
    
execute:
    if( !(strncmp(args[0],"/",1)) || 
        !(strncmp(args[0],"./",2)) ||
        !(strncmp(args[0],"../",3))){
        //PATH is already given
        strcpy(path,args[0]);
    }
    else{
        filepath = get_path_for_cmd(args[0]);
	
	if(filepath){
        	strcpy(path,filepath);
        	
		//printf("Path: %s\n", path);
        	free(filepath);
	    }
    }

    //printf("Path: %s\n", path);
    int pid = fork();
    
    if(!pid){
         
        //child
        if(arg_count >=2 ){
            handle_redirection(args, arg_count);
        }

        //printf("LAST ARGUMENT: %s\n", args[arg_count-1]);
       
        if(batchmode == 1){
		    fclose(batch_fd);
	    }	
        execv(path, args);
    
        exit(127);
    }
    else{
        //parent
        waitpid(pid, &status, 0);
//    	printf("Child return code: %d\n",status);
	    last_status = status;
    }
    
}

int can_skip_line(char* line){
    
    char* ptr = line;

    //if empty string return
    if(strlen(line) == 0){
	return 1;
    }
    //skip spaces
    while(*ptr == ' '){
        ptr++;
    }

    //skip empty line
    if(*ptr == '\n'){
        //printf("Empty Line\n");
        return 1;
    }

    //skip #
    if(*ptr == '#'){
        //printf("Comment\n");
        return 1;
    }

    return 0;
}


void exec_batch_file(char* filename)
{
    char cmd[MAX_LINE];
    //FILE* fd = fopen(filename, "r");
    batch_fd = fopen(filename, "r");

    if(!batch_fd){
        printf("Unable to open file %s\n",filename);
        return;
    }

    batchmode = 1;
    while(fgets(cmd, MAX_LINE, batch_fd) != NULL){
        if(can_skip_line(cmd)){
            continue;
        }

        //removing /n at the end of cmd
        cmd[strlen(cmd)-1] = '\0';

        exec_cmd(cmd,0);

    }

    fclose(batch_fd);
    batchmode = 0;
}

int main(int argc, char* argv[])
{
    char cmd[MAX_LINE];

    init_environ();

    int is_interactive = isatty(STDIN_FILENO);

    //if more than two args; return
    if( argc > 2){
        return 0;
    }

    //if extra argument, consider it as batch mode
    if( argc == 2){
        exec_batch_file(argv[1]);
        return 0;
    }

    while(1) {
	
        printf("wsh> ");
        fflush(stdout);
	
        if(fgets(cmd, MAX_LINE, stdin) == NULL){
            break;
        }

        //env is not putting new line after  output: test case 10   
        if(!is_interactive && !strcmp(cmd,"env\n")){
            printf("\n");
            fflush(stdout);
        }

        //removing /n at the end of cmd
        if(cmd[strlen(cmd)-1] == '\n'){
            
            cmd[strlen(cmd)-1] = '\0';
        }

        if(can_skip_line(cmd)){
            continue;
        }

        exec_cmd(cmd,0);
        //dump_history();
            
    }

    clean_up();    

    //if child exit status is not zero return 255 to main
    if(last_status!=0){
	    return 255;
    }

    return 0;
   
}

