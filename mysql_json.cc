/*
 * Copyright 2011 Kazuho Oku. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY KAZUHO OKU ''AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL KAZUHO OKU OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of Kazuho Oku.
 */

extern "C" {
#include <mysql/mysql.h>
#include <ctype.h>
#include <limits.h>
}
#include <string>
#include "picojson/picojson.h"

extern "C" {
my_bool json_get_init(UDF_INIT* initid, UDF_ARGS* args, char* message);
void json_get_deinit(UDF_INIT* initid);
char* json_get(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error);
}

using namespace std;

my_bool json_get_init(UDF_INIT* initid, UDF_ARGS* args, char* message)
{
  if (args->arg_count < 1) {
    strcpy(message, "json_get: too few arguments");
    return 1;
  }
  if (args->arg_type[0] != STRING_RESULT) {
    strcpy(message, "json_get: 1st argument should be a string");
    return 1;
  }
  // assert (or convert) succeeding arguments to either int or string
  for (unsigned i = 1; i < args->arg_count; ++i) {
    switch (args->arg_type[i]) {
    case INT_RESULT:
    case STRING_RESULT:
      break;
    default:
      args->arg_type[i] = STRING_RESULT;
      break;
    }
  }
  initid->ptr = (char*)(void*)new std::string();
  initid->const_item = 1;
  return 0;
}

void json_get_deinit(UDF_INIT* initid)
{
  delete (std::string*)(void*)initid->ptr;
}

char* json_get(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error)
{
  picojson::value value;
  
  { // parse json
    string err = picojson::parse(value, args->args[0],
				 args->args[0] + args->lengths[0]);
    if (! err.empty()) {
      fprintf(stderr, "json_get: invalid json string: %s\n", err.c_str());
      *error = 1;
      return NULL;
    }
  }
  
  // track down the object
  const picojson::value* target = &value;
  for (unsigned i = 1; i < args->arg_count; ++i) {
    
    if (target->is<picojson::array>()) {
      
      // is an array, fetch by index
      size_t idx;
      switch (args->arg_type[i]) {
      case INT_RESULT:
	// TODO check overflow / underflow
	idx = (size_t)*(long long*)args->args[i];
	break;
      case REAL_RESULT:
	// TODO check overflow / underflow
	idx = (size_t)*(double*)args->args[i];
	break;
      case STRING_RESULT:
	idx = 0;
	for (const char* p = args->args[i], * pMax = p + args->lengths[i];
	     p != pMax;
	     ++p) {
	  if (isspace(*p)) {
	  } else if ('0' <= *p && *p <= '9') {
	    idx = idx * 10 + *p - '0';
	  } else {
	    break;
	  }
	}
	break;
      default:
	assert(0);
	idx = 0; // suppress compiler warning
	break;
      }
      target = &target->get(idx);
      
    } else if (target->is<picojson::object>()) {
      
      // is an object, fetch by property name
      std::string key;
      switch (args->arg_type[i]) {
      case INT_RESULT:
	{
	  char buf[64];
	  sprintf(buf, "%lld", *(long long*)args->args[i]);
	  key = buf;
	}
	break;
      case REAL_RESULT:
	{
	  char buf[64];
	  sprintf(buf, "%f", *(double*)args->args[i]);
	  key = buf;
	}
	break;
      case STRING_RESULT:
	key.assign(args->args[i], args->lengths[i]);
	break;
      default:
	assert(0);
	break;
      }
      target = &target->get(key);
      
    } else {
      
      // failed to obtain value, return null
      target = NULL;
      break;
      
    }
    
  }
  
  // setup the result and return
  const char* ret;
  if (target == NULL || target->is<picojson::null>()) {
    ret = NULL;
    *length = 0;
    *is_null = 1;
  } else {
    std::string* ret_s = (std::string*)(void*)initid->ptr;
    *ret_s = target->to_str();
    ret = &(*ret_s)[0];
    *length = ret_s->size();
    *is_null = 0;
  }
  return const_cast<char*>(ret);
}
