#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <mysql/mysql.h>

#define FUNC_NAME		json_get

#define CONCAT2(x,y)		x##y
#define CONCAT(x,y)		CONCAT2(x,y)
#define FUNC_NAME_SUF(suf)	CONCAT(FUNC_NAME, suf)
#define MAX_ARGS		1024
#define STRINGIFY(x)		#x
#define STR(x)			STRINGIFY(x)

extern "C" {
    my_bool FUNC_NAME_SUF(_init)(UDF_INIT* initid, UDF_ARGS* args, char* message);
    void FUNC_NAME_SUF(_deinit)(UDF_INIT* initid);
    char* FUNC_NAME(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error);
}

int main(int argc, char *argv[])
{
    UDF_INIT uinit;
    UDF_ARGS uargs;
    char message[MYSQL_ERRMSG_SIZE], result[1024];
    unsigned long length;
    char is_null, error;
    enum Item_result arg_types[MAX_ARGS];
    long long long_args[MAX_ARGS];
    unsigned long lengths[MAX_ARGS], attribute_lengths[MAX_ARGS];
    char maybe_null[MAX_ARGS];
    char attributes[MAX_ARGS][8];
    char *attr_ptrs[MAX_ARGS];
    const char *ret_res;
    char *long_end;
    int arg;

    uinit.maybe_null = true;
    uinit.decimals = 31;
    uinit.max_length = 1024;
    uinit.ptr = NULL;
    uinit.const_item = false;
    uinit.extension = NULL;

    uargs.arg_count = argc - 1;
    uargs.arg_type = arg_types;
    uargs.args = argv + 1;
    uargs.lengths = lengths;
    uargs.maybe_null = maybe_null;
    uargs.attributes = attr_ptrs;
    uargs.attribute_lengths = attribute_lengths;
    uargs.extension = NULL;

    for (arg = 0; arg < argc - 1; arg++) {
	errno = 0;
	if ((isdigit(*argv[arg + 1]) || *argv[arg + 1] == '-' || *argv[arg + 1] == '+') &&
	  !(long_args[arg] = strtoll(argv[arg + 1], &long_end, 10), errno) && !*long_end) {
	    arg_types[arg] = INT_RESULT;
	    lengths[arg] = sizeof(long_args[arg]);
	    argv[arg + 1] = (char *)&long_args[arg];
	} else {
	    arg_types[arg] = STRING_RESULT;
	    lengths[arg] = strlen(argv[arg + 1]);
	    if (!strcmp(argv[arg + 1], "NULL")) {
		argv[arg + 1] = NULL;
	    }
	}
	maybe_null[arg] = false;
	sprintf(attributes[arg], "arg%04d", arg);
	attribute_lengths[arg] = strlen(attributes[arg]);
	attr_ptrs[arg] = attributes[arg];
    }
    if (FUNC_NAME_SUF(_init)(&uinit, &uargs, message)) {
	fprintf(stderr, STR(FUNC_NAME) " init error: %s\n", message);
	return 1;
    }

    is_null = error = 0;
    ret_res = FUNC_NAME(&uinit, &uargs, result, &length, &is_null, &error);
    if (error) {
	fprintf(stderr, STR(FUNC_NAME) " returned error\n");
	FUNC_NAME_SUF(_deinit)(&uinit);
	return 2;
    }
    if (is_null) {
	fprintf(stderr, STR(FUNC_NAME) " returned NULL\n");
    } else {
	fwrite(ret_res, 1, length, stdout);
	putchar('\n');
    }
    FUNC_NAME_SUF(_deinit)(&uinit);
    return 0;
}
