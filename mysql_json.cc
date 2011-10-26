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
  args->maybe_null[0] = 0;
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
    args->maybe_null[i] = 0;
  }
  initid->ptr = (char*)(void*)new std::string();
  initid->const_item = 1;
  return 0;
}

void json_get_deinit(UDF_INIT* initid)
{
  delete (std::string*)(void*)initid->ptr;
}

namespace {
  
  struct global_context {
    UDF_ARGS* args;
    std::string* out;
    std::string* buff;
    global_context(UDF_ARGS* a, std::string* b) : args(a), out(NULL), buff(b) {}
  };
  
  class filtered_context {
  protected:
    global_context* g_ctx_;
    unsigned arg_index_;
    size_t wanted_array_index_;
    std::string wanted_object_property_;
  public:
    filtered_context(global_context* g_ctx, unsigned arg_index) : g_ctx_(g_ctx), arg_index_(arg_index) {}
    bool set_null() {
      // out should have been initialized to NULL
      return true;
    }
    bool set_bool(bool b) {
      _set_if_leaf(picojson::value(b));
      return true;
    }
    bool set_number(float f) {
      _set_if_leaf(picojson::value(f));
      return true;
    }
    template <typename Iter> bool parse_string(picojson::input<Iter>& in) {
      if (_is_leaf()) {
	std::string& s = _returning_buffered_str();
	s.clear();
	return _parse_string(s, in);
      } else {
	picojson::null_parse_context::dummy_str dummy;
	return _parse_string(dummy, in);
      }
    }
    bool parse_array_start() {
      if (_is_leaf()) {
	_returning_buffered_str() = "array";
      } else {
	UDF_ARGS* args = g_ctx_->args;
	size_t idx = 0;
	switch (args->arg_type[arg_index_]) {
	case INT_RESULT:
	  // TODO check overflow / underflow
	  idx = (size_t)*(long long*)args->args[arg_index_];
	  break;
	case REAL_RESULT:
	  // TODO check overflow / underflow
	  idx = (size_t)*(double*)args->args[arg_index_];
	  break;
	case STRING_RESULT:
	  idx = 0;
	  for (const char* p = args->args[arg_index_],
		 * pMax = p + args->lengths[arg_index_];
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
	  break;
	}
	wanted_array_index_ = idx;
      }
      return true;
    }
    template <typename Iter> bool parse_array_item(picojson::input<Iter>& in, size_t idx) {
      if (! _is_leaf() && idx == wanted_array_index_) {
	filtered_context ctx(g_ctx_, arg_index_ + 1);
	return _parse(ctx, in);
      } else {
	picojson::null_parse_context ctx;
	return _parse(ctx, in);
      }
    }
    bool parse_object_start() {
      if (_is_leaf()) {
	_returning_buffered_str() = "object";
      } else {
	UDF_ARGS* args = g_ctx_->args;
	switch (args->arg_type[arg_index_]) {
	case INT_RESULT:
	  {
	    char buf[64];
	    sprintf(buf, "%lld", *(long long*)args->args[arg_index_]);
	    wanted_object_property_ = buf;
	  }
	  break;
	case REAL_RESULT:
	  {
	    char buf[64];
	    sprintf(buf, "%f", *(double*)args->args[arg_index_]);
	    wanted_object_property_ = buf;
	  }
	  break;
	case STRING_RESULT:
	  wanted_object_property_.assign(args->args[arg_index_],
					 args->lengths[arg_index_]);
	  break;
	default:
	  assert(0);
	  break;
	}
      }
      return true;
    }
    template <typename Iter> bool parse_object_item(picojson::input<Iter>& in, const std::string& key) {
      if (! _is_leaf() && key == wanted_object_property_) {
	filtered_context ctx(g_ctx_, arg_index_ + 1);
	return _parse(ctx, in);
      } else {
	picojson::null_parse_context ctx;
	return _parse(ctx, in);
      }
    }
  private:
    std::string& _returning_buffered_str() {
      return *(g_ctx_->out = g_ctx_->buff);
    }
    bool _is_leaf() {
      return arg_index_ == g_ctx_->args->arg_count;
    }
    void _set_if_leaf(const picojson::value& v) {
      if (_is_leaf())
	_returning_buffered_str() = v.to_str();
    }
  };
}

char* json_get(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error)
{
  global_context g_ctx(args, (std::string*)(void*)initid->ptr);
  filtered_context ctx(&g_ctx, 1);
  
  std::string err;
  picojson::_parse(ctx, args->args[0], args->args[0] + args->lengths[0],
		   &err);
  if (! err.empty()) {
    fprintf(stderr, "json_get: invalid json string: %s\n", err.c_str());
    *error = 1;
    return NULL;
  }
  
  if (g_ctx.out == NULL) {
    *length = 0;
    *is_null = 1;
    return NULL;
  }
  *length = g_ctx.out->size();
  return &(*g_ctx.out)[0];
}
