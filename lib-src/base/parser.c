/* BLURB lgpl

                           Coda File System
                              Release 6

          Copyright (c) 1987-2003 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the  terms of the  GNU  Library General Public Licence  Version 2,  as
shown in the file LICENSE. The technical and financial contributors to
Coda are listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "coda_string.h"
#include <stddef.h>
#include <sys/param.h>
#include "coda_assert.h"

#include <readline/readline.h>
#include <readline/history.h>

#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifndef HAVE_RL_COMPLETION_MATCHES
/* compatibility for readline libs < v4.2 */
#define rl_completion_matches completion_matches
#endif

#include "parser.h"
#define CMD_COMPLETE 0
#define CMD_INCOMPLETE 1
#define CMD_NONE 2
#define CMD_AMBIG 3

static command_t * top_level;	   /* Top level of commands, initialized by
				    * InitParser  			    */
static command_t * match_tbl;	   /* Command completion against this table */
static char * parser_prompt = NULL;/* Parser prompt, set by InitParser      */
static int done;		   /* Set to 1 if user types exit or quit   */


/* static functions */
static char *skipwhitespace(char *s);
static char *skiptowhitespace(char *s);
static command_t *find_cmd(char *name, command_t cmds[], char **next);
static int process(char *s, char **next, command_t *lookup, command_t **result, char **prev);
static char *command_generator(const char *text, int state);
static char **command_completion(const char *text, int start, int end);
static void print_commands(const char *str, command_t *table);

#if 1
static char * skipwhitespace(char * s)
{
    char * t;
    int    len;

    len = (int)strlen(s);

    for (t = s; t <= s + len && isspace((int)*t); t++);
    return(t);
}

static char * skiptowhitespace(char * s) 
{
    char * t;
    
    for (t = s; *t && !isspace((int)*t); t++);
    return(t);
}

#else

char * skipwhitespace(char *a) {
   while (*a != '\0' && isspace(*a))
	a++;
   return a;
}

char * skiptowhitespace(char *a) {
   while (*a != '\0' && !isspace(*a))
	a++;
   return a;
}
#endif

int line2args(char *line, char **argv, int maxargs)
{
    char *arg;
    int i = 0; 
    
    arg = strtok(line, " \t");
    if ( arg ) {
	argv[i] = arg;
	i++;
    } else 
	return 0;

    while( (arg = strtok(NULL, " \t")) && (i <= maxargs)) {
	argv[i] = arg;
	i++;
    }
    return i;
}

/* find a command -- return it if unique otherwise print alternatives */
    
static argcmd_t *Parser_findargcmd(char *name, argcmd_t cmds[])
{
	argcmd_t *cmd;
	int i;

	for (i = 0; cmds[i].ac_name; i++) {
		cmd = &cmds[i];
		if (strcmp(name, cmd->ac_name) == 0) 
		    return cmd;
	}
	return NULL;
}

int Parser_execarg(int argc, char **argv, argcmd_t cmds[])
{
	argcmd_t *cmd;
	int i;

        cmd = Parser_findargcmd(argv[0], cmds);
	if ( cmd )
		return (cmd->ac_func)(argc, argv);

	printf("Try interactive use without arguments or use one of: ");
	for (i=0 ; cmds[i].ac_name ; i++) {
	    cmd = &cmds[i];
	    printf("\"%s\" ", cmd->ac_name);
	}
	printf("as argument.\n");

	return -1;
}

/* returns the command_t * (NULL if not found) corresponding to a
   _partial_ match with the first token in name.  It sets *next to
   point to the following token. Does not modify *name. */
static command_t * find_cmd(char * name, command_t cmds[], char ** next) 
{
    int    i, len;
    
    if (!cmds || !name ) 
        return NULL;
    
    /* This sets name to point to the first non-white space character,
    and next to the first whitespace after name, len to the length: do
    this with strtok*/
    name = skipwhitespace(name);
    *next = skiptowhitespace(name);
    len = *next - name;
    if (len == 0) 
	return NULL;

    for (i = 0; cmds[i].name; i++) {
	if (strncasecmp(name, cmds[i].name, len) == 0) {
	    *next = skipwhitespace(*next);
	    return(&cmds[i]);
	}
    }
    return NULL;
}

/* Recursively process a command line string s and find the command
   corresponding to it. This can be ambiguous, full, incomplete,
   non-existent. */
static int process(char *s, char ** next, command_t *lookup,
		   command_t **result, char **prev)
{
    *result = find_cmd(s, lookup, next);
    *prev = s; 

    /* non existent */
    if ( ! *result ) 
	return CMD_NONE;

    /* found entry: is it ambigous, i.e. not exact command name and
       more than one command in the list matches.  Note that find_cmd
       points to the first ambiguous entry */
    if ( strncasecmp(s, (*result)->name, strlen((*result)->name)) &&
	 find_cmd(s, (*result) + 1, next)) 
	return CMD_AMBIG;

    /* found a unique command: component or full? */
    if ( (*result)->func ) {
	return CMD_COMPLETE;
    } else {
	if ( *next == '\0' ) {
	    return CMD_INCOMPLETE;
	} else {
	    return process(*next, next, (*result)->sub_cmd, result, prev);
	}
    }
}

static char *command_generator(const char *text, int state)
{
    static int index, len;
    const char *name;

    /* Do we have a match table? */
    if (!match_tbl)
	return NULL;

    /* If this is the first time called on this word, state is 0 */
    if (!state) {
	index = 0;
	len = (int)strlen(text);
    }

    /* Return the next name in the command list that paritally matches test */
    while ((name = (match_tbl + index)->name) != NULL)
    {
	index++;

	if (strncasecmp(name, text, len) == 0)
	    return strdup(name);
    }

    /* No more matches */
    return NULL;
}

/* probably called by readline */
static char **command_completion(const char *text, int start, int end)
{
    command_t *table;
    char *pos;

    match_tbl = top_level;
    pos = rl_line_buffer;

    while((table = find_cmd(pos, match_tbl, &pos)) != NULL)
	if (*(pos - 1) == ' ')
	    match_tbl = table->sub_cmd;

    return rl_completion_matches(text, command_generator);
}

/* take a string and execute the function or print help */
void execute_line(char * line) 
{
    command_t 	*cmd, *ambig;
    char *prev;
    char *next, *tmp;
    char *argv[MAXARGS];
    int	 i;

    switch( process(line, &next, top_level, &cmd, &prev) ) {
    case CMD_AMBIG:
	fprintf(stderr, "Ambiguous command \'%s\'\nOptions: ", line);
	while( (ambig = find_cmd(prev, cmd, &tmp)) ) {
	    fprintf(stderr, "%s ", ambig->name);
	    cmd = ambig + 1;
	}
	fprintf(stderr, "\n");
	break;
    case CMD_NONE:
	fprintf(stderr, "No such command, type help\n");
	break;
    case CMD_INCOMPLETE:
	fprintf(stderr,
		"'%s' incomplete command.  Use '%s x' where x is one of:\n",
		line, line);
	fprintf(stderr, "\t");
	for (i = 0; cmd->sub_cmd[i].name; i++) {
	    fprintf(stderr, "%s ", cmd->sub_cmd[i].name);
	}
	fprintf(stderr, "\n");
	break;
    case CMD_COMPLETE:
	i = line2args(line, argv, MAXARGS);
	(cmd->func)(i, argv);
	break;
    }
    
    return;
}

/* this is the command execution machine */
void Parser_commands(void)
{
    char *line,
	 *s;

    using_history();
    stifle_history(HISTORY);

    rl_attempted_completion_function = command_completion;
    rl_completion_entry_function = &command_generator;

    while(!done) {
	line = readline(parser_prompt);

	if (!line) break;

	s = skipwhitespace(line);

	if (*s) {
	    add_history(s);
	    execute_line(s);
	}

	free(line);
    }
}


/* sets the parser prompt */
void Parser_init(const char *prompt, command_t * cmds)
{
    done = 0;
    top_level = cmds;
    if (parser_prompt) free(parser_prompt);
    parser_prompt = strdup(prompt);
}

/* frees the parser prompt */
void Parser_exit(int argc, char *argv[])
{
    done = 1;
    free(parser_prompt);
    parser_prompt = NULL;
}

/* convert a string to an unsigned integer */
int Parser_uint(char *s, unsigned int *val)
{
    int ret;

    if (*s != '0')
	ret = sscanf(s, "%u", val);
    else if (*(s+1) != 'x')
	ret = sscanf(s, "%o", val);
    else {
	s++;
	ret = sscanf(++s, "%x", val);
    }

    return(ret);
}


    
void Parser_qhelp(int argc, char *argv[]) {

    printf("Available commands are:\n");
	
    print_commands(NULL, top_level);
}

void Parser_help(int argc, char **argv) 
{
    char line[1024];
    char *next, *prev, *tmp;
    command_t *result, *ambig;
    int i;

    if ( argc == 1 ) {
	Parser_qhelp(argc, argv);
	return;
    }

    line[0]='\0';
    for ( i = 1 ;  i < argc ; i++ ) {
	strcat(line, argv[i]);
    }

    switch ( process(line, &next, top_level, &result, &prev) ) {
    case CMD_COMPLETE:
	fprintf(stderr, "%s: %s\n",line, result->help);
	break;
    case CMD_NONE:
	fprintf(stderr, "%s: Unknown command.\n", line);
	break;
    case CMD_INCOMPLETE:
	fprintf(stderr,
		"'%s' incomplete command.  Use '%s x' where x is one of:\n",
		line, line);
	fprintf(stderr, "\t");
	for (i = 0; result->sub_cmd[i].name; i++) {
	    fprintf(stderr, "%s ", result->sub_cmd[i].name);
	}
	fprintf(stderr, "\n");
	break;
    case CMD_AMBIG:
	fprintf(stderr, "Ambiguous command \'%s\'\nOptions: ", line);
	while( (ambig = find_cmd(prev, result, &tmp)) ) {
	    fprintf(stderr, "%s ", ambig->name);
	    result = ambig + 1;
	}
	fprintf(stderr, "\n");
	break;
    }
    return;
}  

/*************************************************************************
 * COMMANDS								 *
 *************************************************************************/ 

static void print_commands(const char *str, command_t *table)
{
    command_t *cmds;
    char buf[80];

    for (cmds = table; cmds->name; cmds++) {
	if (cmds->func) {
	    if (str) printf("\t%s %s\n", str, cmds->name);
	    else printf("\t%s\n", cmds->name);
	}
	if (cmds->sub_cmd) {
	    if (str) {
		sprintf(buf, "%s %s", str, cmds->name);
		print_commands(buf, cmds->sub_cmd);
	    } else {
		print_commands(cmds->name, cmds->sub_cmd);
	    }
	}
    }
}

char *Parser_getstr(const char *prompt, const char *deft, char *res, 
		    size_t len)
{
    char *line = NULL;
    int size = strlen(prompt) + strlen(deft) + 8;
    char *theprompt;
    theprompt = malloc(size);
    CODA_ASSERT(theprompt);

    sprintf(theprompt, "%s [%s]: ", prompt, deft);

    line  = readline(theprompt);
    free(theprompt);

    if ( line == NULL || *line == '\0' ) {
	strncpy(res, deft, len);
    } else {
	strncpy(res, line, len);
    }

    if ( line ) {
	free(line);
	return res;
    } else {
	return NULL;
    }
}

/* get integer from prompt, loop forever to get it */
int Parser_getint(const char *prompt, long min, long max, long deft, int base)
{
    int rc;
    long result;
    char *line;
    int size = strlen(prompt) + 40;
    char *theprompt = malloc(size);
    CODA_ASSERT(theprompt);
    sprintf(theprompt,"%s [%ld, (0x%lx)]: ", prompt, deft, deft);

    fflush(stdout);

    do {
	line = NULL;
	line = readline(theprompt);
	if ( !line ) {
	    fprintf(stdout, "Please enter an integer.\n");
	    fflush(stdout);
	    continue;
	}
	if ( *line == '\0' ) {
	    free(line);
	    result =  deft;
	    break;
	}
	rc = Parser_arg2int(line, &result, base);
	free(line);
	if ( rc != 0 ) {
	    fprintf(stdout, "Invalid string.\n");
	    fflush(stdout);
	} else if ( result > max || result < min ) {
	    fprintf(stdout, "Error: response must lie between %ld and %ld.\n",
		    min, max);
	    fflush(stdout);
	} else {
	    break;
	}
    } while ( 1 ) ;

    if (theprompt)
	free(theprompt);
    return result;

}

/* get boolean (starting with YyNn; loop forever */
int Parser_getbool(const char *prompt, int deft)
{
    int result = 0;
    char *line;
    int size = strlen(prompt) + 8;
    char *theprompt = malloc(size);
    CODA_ASSERT(theprompt);

    fflush(stdout);
    
    if ( deft != 0 && deft != 1 ) {
	fprintf(stderr, "Error: Parser_getbool given bad default (%d).\n",
		deft);
	CODA_ASSERT ( 0 );
    }
    sprintf(theprompt, "%s [%s]: ", prompt, (deft==0)? "N" : "Y");

    do {
	line = NULL;
	line = readline(theprompt);
	if ( line == NULL ) {
	    result = deft;
	    break;
	}
	if ( *line == '\0' ) {
	    result = deft;
	    break;
	}
	if ( *line == 'y' || *line == 'Y' ) {
	    result = 1;
	    break;
	}
	if ( *line == 'n' || *line == 'N' ) {
	    result = 0;
	    break;
	}
	if ( line ) 
	    free(line);
	fprintf(stdout, "Invalid string. Must start with yY or nN\n");
	fflush(stdout);
    } while ( 1 );

    if ( line ) 
	free(line);
    if ( theprompt ) 
	free(theprompt);
    return result;
}

/* parse int out of a string or prompt for it */
long Parser_intarg(const char *inp, const char *prompt, int deft,
		  int min, int max, int base)
{
    long result;
    int rc; 
    
    rc = Parser_arg2int(inp, &result, base);

    if ( rc == 0 ) {
	return result;
    } else {
	return Parser_getint(prompt, deft, min, max, base);
    }
}

/* parse int out of a string or prompt for it */
char *Parser_strarg(char *inp, const char *prompt, const char *deft,
		    char *answer, int len)
{
    
    if ( inp == NULL || *inp == '\0' ) {
	return Parser_getstr(prompt, deft, answer, len);
    } else 
	return inp;
}

/* change a string into a number: return 0 on success. No invalid characters
   allowed. The processing of base and validity follows strtol(3)*/
int Parser_arg2int(const char *inp, long *result, int base)
{
    char *endptr;

    if ( (base !=0) && (base < 2 || base > 36) )
	return 1;

    *result = strtol(inp, &endptr, base);

    if ( *inp != '\0' && *endptr == '\0' )
	return 0;
    else 
	return 1;
}
