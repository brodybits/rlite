#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "hirlite.h"
#include "util.h"
#include "constants.h"

#define UNSIGN(val) ((unsigned char *)val)

#define RLITE_SERVER_ERR(c, retval)\
	if (retval == RL_WRONG_TYPE) {\
		c->reply = createErrorObject(RLITE_WRONGTYPEERR);\
		goto cleanup;\
	}\
	if (retval == RL_NAN) {\
		c->reply = createErrorObject("resulting score is not a number (NaN)");\
		goto cleanup;\
	}\

struct rliteCommand *lookupCommand(const char *name, size_t UNUSED(len));
void __rliteSetError(rliteContext *c, int type, const char *str);

static rliteReply *createReplyObject(int type) {
	rliteReply *r = calloc(1,sizeof(*r));

	if (r == NULL)
		return NULL;

	r->type = type;
	return r;
}

static rliteReply *createStringTypeObject(int type, const char *str, const int len) {
	rliteReply *reply = createReplyObject(type);
	reply->str = malloc(sizeof(char) * len);
	if (!reply->str) {
		freeReplyObject(reply);
		return NULL;
	}
	memcpy(reply->str, str, len);
	reply->len = len;
	return reply;
}

static rliteReply *createStringObject(const char *str, const int len) {
	return createStringTypeObject(RLITE_REPLY_STRING, str, len);
}

static rliteReply *createCStringObject(const char *str) {
	return createStringObject(str, strlen(str));
}

static rliteReply *createErrorObject(const char *str) {
	return createStringTypeObject(RLITE_REPLY_ERROR, str, strlen((char *)str));
}

static rliteReply *createDoubleObject(double d) {
	char dbuf[128];
	int dlen;
	if (isinf(d)) {
		/* Libc in odd systems (Hi Solaris!) will format infinite in a
		 * different way, so better to handle it in an explicit way. */
		return createCStringObject(d > 0 ? "inf" : "-inf");
	} else {
		dlen = snprintf(dbuf,sizeof(dbuf),"%.17g",d);
		return createStringObject(dbuf, dlen);
	}
}

static rliteReply *createLongLongObject(long long value) {
	rliteReply *reply = createReplyObject(RLITE_REPLY_INTEGER);
	reply->integer = value;
	return reply;
}

static void addZsetIteratorReply(rliteClient *c, int retval, rl_zset_iterator *iterator, int withscores)
{
	unsigned char *vstr;
	long vlen, i;
	double score;

	c->reply = createReplyObject(RLITE_REPLY_ARRAY);
	if (retval == RL_NOT_FOUND) {
		c->reply->elements = 0;
		return;
	}
	c->reply->elements = withscores ? (iterator->size * 2) : iterator->size;
	c->reply->element = malloc(sizeof(rliteReply*) * c->reply->elements);
	i = 0;
	while ((retval = rl_zset_iterator_next(iterator, withscores ? &score : NULL, &vstr, &vlen)) == RL_OK) {
		c->reply->element[i] = createStringObject((char *)vstr, vlen);
		i++;
		if (withscores) {
			c->reply->element[i] = createDoubleObject(score);
			i++;
		}
		rl_free(vstr);
	}

	if (retval != RL_END) {
		__rliteSetError(c->context, RLITE_ERR, "Unexpected early end");
		goto cleanup;
	}
	iterator = NULL;
cleanup:
	if (iterator) {
		rl_zset_iterator_destroy(iterator);
	}
}

static int addReply(rliteContext* c, rliteReply *reply) {
	if (c->replyPosition == c->replyAlloc) {
		void *tmp;
		c->replyAlloc *= 2;
		tmp = realloc(c->replies, sizeof(rliteReply*) * c->replyAlloc);
		if (!tmp) {
			__rliteSetError(c,RLITE_ERR_OOM,"Out of memory");
			return RLITE_ERR;
		}
		c->replies = tmp;
	}

	c->replies[c->replyPosition] = reply;
	c->replyLength++;
	return RLITE_OK;
}

static int addReplyErrorFormat(rliteContext *c, const char *fmt, ...) {
	int maxlen = strlen(fmt) * 2;
	char *str = malloc(maxlen * sizeof(char));
	va_list ap;
	va_start(ap, fmt);
	int written = vsnprintf(str, maxlen, fmt, ap);
	va_end(ap);
	if (written < 0) {
		fprintf(stderr, "Failed to vsnprintf near line %d, got %d\n", __LINE__, written);
		free(str);
		return RLITE_ERR;
	}
	rliteReply *reply = createReplyObject(RLITE_REPLY_ERROR);
	if (!reply) {
		free(str);
		__rliteSetError(c,RLITE_ERR_OOM,"Out of memory");
		return RLITE_ERR;
	}
	reply->str = str;
	addReply(c, reply);
	return RLITE_OK;
}

int getDoubleFromObject(const char *o, double *target) {
	double value;
	char *eptr;

	if (o == NULL) {
		value = 0;
	} else {
		errno = 0;
		value = strtod(o, &eptr);
		if (isspace(((char*)o)[0]) || eptr[0] != '\0' ||
				(errno == ERANGE && (value == HUGE_VAL ||
									 value == -HUGE_VAL || value == 0)) ||
				errno == EINVAL || isnan(value))
		return RLITE_ERR;
	}
	*target = value;
	return RLITE_OK;
}

static int getDoubleFromObjectOrReply(rliteClient *c, const char *o, double *target, const char *msg) {
	if (getDoubleFromObject(o, target) != RLITE_OK) {
		if (msg != NULL) {
			c->reply = createErrorObject(msg);
		} else {
			c->reply = createErrorObject("value is not a valid float");
		}
		return RLITE_ERR;
	}
	return RLITE_OK;
}

int getLongLongFromObject(const char *o, long long *target) {
	long long value;
	char *eptr;

	if (o == NULL) {
		value = 0;
	} else {
		errno = 0;
		value = strtoll(o, &eptr, 10);
		if (isspace(((char*)o)[0]) || eptr[0] != '\0' || errno == ERANGE) {
			return RLITE_ERR;
		}
	}
	if (target) *target = value;
	return RLITE_OK;
}

int getLongLongFromObjectOrReply(rliteClient *c, const char *o, long long *target, const char *msg) {
	long long value;
	if (getLongLongFromObject(o, &value) != RLITE_OK) {
		if (msg != NULL) {
			c->reply = createErrorObject(msg);
		} else {
			c->reply = createErrorObject("value is not an integer or out of range");
		}
		return RLITE_ERR;
	}
	*target = value;
	return RLITE_OK;
}

int getLongFromObjectOrReply(rliteClient *c, const char *o, long *target, const char *msg) {
	long long value;

	if (getLongLongFromObjectOrReply(c, o, &value, msg) != RLITE_OK) return RLITE_ERR;
	if (value < LONG_MIN || value > LONG_MAX) {
		if (msg != NULL) {
			c->reply = createErrorObject(msg);
		} else {
			c->reply = createErrorObject("value is out of range");
		}
		return RLITE_ERR;
	}
	*target = value;
	return RLITE_OK;
}
void __rliteSetError(rliteContext *c, int type, const char *str) {
	size_t len;

	c->err = type;
	if (str != NULL) {
		len = strlen(str);
		len = len < (sizeof(c->errstr)-1) ? len : (sizeof(c->errstr)-1);
		memcpy(c->errstr,str,len);
		c->errstr[len] = '\0';
	} else {
		/* Only RLITE_ERR_IO may lack a description! */
		assert(type == RLITE_ERR_IO);
		strerror_r(errno,c->errstr,sizeof(c->errstr));
	}
}
/* Free a reply object */
void freeReplyObject(void *reply) {
	rliteReply *r = reply;
	size_t j;

	if (r == NULL)
		return;

	switch(r->type) {
	case RLITE_REPLY_INTEGER:
		break; /* Nothing to free */
	case RLITE_REPLY_ARRAY:
		if (r->element != NULL) {
			for (j = 0; j < r->elements; j++)
				if (r->element[j] != NULL)
					freeReplyObject(r->element[j]);
			free(r->element);
		}
		break;
	case RLITE_REPLY_ERROR:
	case RLITE_REPLY_STATUS:
	case RLITE_REPLY_STRING:
		if (r->str != NULL)
			free(r->str);
		break;
	}
	free(r);
}
int rlitevFormatCommand(char **UNUSED(target), const char *UNUSED(format), va_list UNUSED(ap)) { return 1; }
int rliteFormatCommand(char **UNUSED(target), const char *UNUSED(format), ...) { return 1; }
int rliteFormatCommandArgv(char **UNUSED(target), int UNUSED(argc), const char **UNUSED(argv), const size_t *UNUSED(argvlen)) { return 1; }

#define DEFAULT_REPLIES_SIZE 16
static rliteContext *_rliteConnect(const char *path) {
	rliteContext *context = malloc(sizeof(*context));
	if (!context) {
		return NULL;
	}
	context->replies = malloc(sizeof(rliteReply*) * DEFAULT_REPLIES_SIZE);
	if (!context->replies) {
		free(context);
		context = NULL;
		goto cleanup;
	}
	context->replyPosition = 0;
	context->replyLength = 0;
	context->replyAlloc = DEFAULT_REPLIES_SIZE;
	int retval = rl_open(path, &context->db, RLITE_OPEN_READWRITE | RLITE_OPEN_CREATE);
	if (retval != RL_OK) {
		free(context);
		context = NULL;
		goto cleanup;
	}
cleanup:
	return context;
}

rliteContext *rliteConnect(const char *ip, int UNUSED(port)) {
	return _rliteConnect(ip);
}

rliteContext *rliteConnectWithTimeout(const char *ip, int UNUSED(port), const struct timeval UNUSED(tv)) {
	return _rliteConnect(ip);
}

rliteContext *rliteConnectNonBlock(const char *ip, int UNUSED(port)) {
	return _rliteConnect(ip);
}

rliteContext *rliteConnectBindNonBlock(const char *ip, int UNUSED(port), const char *UNUSED(source_addr)) {
	return _rliteConnect(ip);
}

rliteContext *rliteConnectUnix(const char *path) {
	return _rliteConnect(path);
}

rliteContext *rliteConnectUnixWithTimeout(const char *path, const struct timeval UNUSED(tv)) {
	return _rliteConnect(path);
}

rliteContext *rliteConnectUnixNonBlock(const char *path) {
	return _rliteConnect(path);
}

rliteContext *rliteConnectFd(int UNUSED(fd)) {
	return NULL;
}
int rliteSetTimeout(rliteContext *UNUSED(c), const struct timeval UNUSED(tv)) {
	return 0;
}

int rliteEnableKeepAlive(rliteContext *UNUSED(c)) {
	return 0;
}
void rliteFree(rliteContext *c) {
	rl_close(c->db);
	int i;
	for (i = c->replyPosition; i < c->replyLength; i++) {
		freeReplyObject(c->replies[i]);
	}
	free(c->replies);
	free(c);
}

int rliteFreeKeepFd(rliteContext *UNUSED(c)) {
	return 0;
}

int rliteBufferRead(rliteContext *UNUSED(c)) {
	return 0;
}

int rliteBufferWrite(rliteContext *UNUSED(c), int *UNUSED(done)) {
	return 0;
}

static void *_popReply(rliteContext *c) {
	if (c->replyPosition < c->replyLength) {
		void *ret;
		ret = c->replies[c->replyPosition];
		c->replyPosition++;
		if (c->replyPosition == c->replyLength) {
			c->replyPosition = c->replyLength = 0;
		}
		return ret;
	} else {
		return NULL;
	}
}

int rliteGetReply(rliteContext *c, void **reply) {
	*reply = _popReply(c);
	return RLITE_OK;
}

int __rliteAppendCommandArgv(rliteContext *c, int argc, const char **argv, const size_t *argvlen) {
	if (argc == 0) {
		return RLITE_ERR;
	}

	rliteClient client;
	client.context = c;
	client.argc = argc;
	client.argv = argv;
	client.argvlen = argvlen;

	struct rliteCommand *command = lookupCommand(argv[0], argvlen[0]);
	int retval;
	if (!command) {
		retval = addReplyErrorFormat(c, "unknown command '%s'", (char*)argv[0]);
	} else if ((command->arity > 0 && command->arity != argc) ||
		(argc < -command->arity)) {
		retval = addReplyErrorFormat(c, "wrong number of arguments for '%s' command", command->name);
	} else {
		command->proc(&client);
		retval = addReply(c, client.reply);
	}
	return retval;
}

int rliteAppendFormattedCommand(rliteContext *UNUSED(c), const char *UNUSED(cmd), size_t UNUSED(len)) {
	return RLITE_ERR;
}

int rlitevAppendCommand(rliteContext *UNUSED(c), const char *UNUSED(format), va_list UNUSED(ap)) {
	return RLITE_ERR;
}

int rliteAppendCommand(rliteContext *UNUSED(c), const char *UNUSED(format), ...) {
	return RLITE_ERR;
}

int rliteAppendCommandArgv(rliteContext *c, int argc, const char **argv, const size_t *argvlen) {
	return __rliteAppendCommandArgv(c, argc, argv, argvlen);
}

void *rlitevCommand(rliteContext *c, const char *format, va_list ap) {
	if (rlitevAppendCommand(c,format,ap) != RLITE_OK)
		return NULL;
	return _popReply(c);
}

void *rliteCommand(rliteContext *c, const char *format, ...) {
	va_list ap;
	void *reply = NULL;
	va_start(ap,format);
	reply = rlitevCommand(c,format,ap);
	va_end(ap);
	return reply;
}

void *rliteCommandArgv(rliteContext *c, int argc, const char **argv, const size_t *argvlen) {
	if (rliteAppendCommandArgv(c,argc,argv,argvlen) != RLITE_OK)
		return NULL;
	return _popReply(c);
}

void echoCommand(rliteClient *c)
{
	c->reply = createStringObject(c->argv[1], c->argvlen[1]);
}

void pingCommand(rliteClient *c)
{
	c->reply = createStringObject("PONG", 4);
}

void zaddGenericCommand(rliteClient *c, int incr) {
	const unsigned char *key = UNSIGN(c->argv[1]);
	size_t keylen = c->argvlen[1];
	double score = 0, *scores = NULL;
	int j, elements = (c->argc - 2) / 2;
	int added = 0;

	if (c->argc % 2) {
		c->reply = createErrorObject(RLITE_SYNTAXERR);
		return;
	}

	/* Start parsing all the scores, we need to emit any syntax error
	 * before executing additions to the sorted set, as the command should
	 * either execute fully or nothing at all. */
	scores = malloc(sizeof(double) * elements);
	for (j = 0; j < elements; j++) {
		if (getDoubleFromObjectOrReply(c,c->argv[2+j*2],&scores[j],NULL)
			!= RLITE_OK) goto cleanup;
	}

	int retval;
	for (j = 0; j < elements; j++) {
		score = scores[j];
		if (incr) {
			retval = rl_zincrby(c->context->db, key, keylen, score, UNSIGN(c->argv[3+j*2]), c->argvlen[3+j*2], NULL);
			RLITE_SERVER_ERR(c, retval);
		} else {
			retval = rl_zadd(c->context->db, key, keylen, score, UNSIGN(c->argv[3+j*2]), c->argvlen[3+j*2]);
			RLITE_SERVER_ERR(c, retval);
			if (retval == RL_OK) {
				added++;
			}
		}
	}
	if (incr) /* ZINCRBY */
		c->reply = createDoubleObject(score);
	else /* ZADD */
		c->reply = createLongLongObject(added);

cleanup:
	free(scores);
}

void zaddCommand(rliteClient *c) {
	zaddGenericCommand(c,0);
}

void zincrbyCommand(rliteClient *c) {
	zaddGenericCommand(c,1);
}

void zrangeGenericCommand(rliteClient *c, int reverse) {
	rl_zset_iterator *iterator;
	int withscores = 0;
	long start;
	long end;

	if ((getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != RLITE_OK) ||
		(getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != RLITE_OK)) return;

	if (c->argc == 5 && !strcasecmp(c->argv[4], "withscores")) {
		withscores = 1;
	} else if (c->argc >= 5) {
		c->reply = createErrorObject(RLITE_SYNTAXERR);
		return;
	}

	int retval = (reverse ? rl_zrevrange : rl_zrange)(c->context->db, UNSIGN(c->argv[1]), c->argvlen[1], start, end, &iterator);
	addZsetIteratorReply(c, retval, iterator, withscores);
}

void zrangeCommand(rliteClient *c) {
	zrangeGenericCommand(c, 0);
}

void zrevrangeCommand(rliteClient *c) {
	zrangeGenericCommand(c, 1);
}

void zremCommand(rliteClient *c) {
	const unsigned char *key = UNSIGN(c->argv[1]);
	const size_t keylen = c->argvlen[1];
	long deleted = 0;
	int j;

	// memberslen needs long, we have size_t (unsigned long)
	// it would be great not to need this
	long *memberslen = malloc(sizeof(long) * (c->argc - 2));
	if (!memberslen) {
		__rliteSetError(c->context, RLITE_ERR_OOM, "Out of memory");
		goto cleanup;
	}
	for (j = 2; j < c->argc; j++) {
		memberslen[j - 2] = c->argvlen[j];
	}
	int retval = rl_zrem(c->context->db, key, keylen, c->argc - 2, (unsigned char **)&c->argv[2], (long *)&c->argvlen[2], &deleted);
	free(memberslen);
	RLITE_SERVER_ERR(c, retval);

	c->reply = createLongLongObject(deleted);
cleanup:
	return;
}

/* Populate the rangespec according to the objects min and max. */
static int zslParseRange(const char *min, const char *max, rl_zrangespec *spec) {
	char *eptr;
	spec->minex = spec->maxex = 0;

	/* Parse the min-max interval. If one of the values is prefixed
	 * by the "(" character, it's considered "open". For instance
	 * ZRANGEBYSCORE zset (1.5 (2.5 will match min < x < max
	 * ZRANGEBYSCORE zset 1.5 2.5 will instead match min <= x <= max */
	if (min[0] == '(') {
		spec->min = strtod((char*)min+1,&eptr);
		if (eptr[0] != '\0' || isnan(spec->min)) return RLITE_ERR;
		spec->minex = 1;
	} else {
		spec->min = strtod((char*)min,&eptr);
		if (eptr[0] != '\0' || isnan(spec->min)) return RLITE_ERR;
	}
	if (((char*)max)[0] == '(') {
		spec->max = strtod((char*)max+1,&eptr);
		if (eptr[0] != '\0' || isnan(spec->max)) return RLITE_ERR;
		spec->maxex = 1;
	} else {
		spec->max = strtod((char*)max,&eptr);
		if (eptr[0] != '\0' || isnan(spec->max)) return RLITE_ERR;
	}

	return RLITE_OK;
}
/* Implements ZREMRANGEBYRANK, ZREMRANGEBYSCORE, ZREMRANGEBYLEX commands. */
#define ZRANGE_RANK 0
#define ZRANGE_SCORE 1
#define ZRANGE_LEX 2
void zremrangeGenericCommand(rliteClient *c, int rangetype) {
	int retval;
	long deleted;
	rl_zrangespec rlrange;
	long start, end;

	/* Step 1: Parse the range. */
	if (rangetype == ZRANGE_RANK) {
		if ((getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != RLITE_OK) ||
			(getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != RLITE_OK))
			return;
		retval = rl_zremrangebyrank(c->context->db, UNSIGN(c->argv[1]), c->argvlen[1], start, end, &deleted);
	} else if (rangetype == ZRANGE_SCORE) {
		if (zslParseRange(c->argv[2],c->argv[3],&rlrange) != RLITE_OK) {
			c->reply = createErrorObject("min or max is not a float");
			return;
		}
		retval = rl_zremrangebyscore(c->context->db, UNSIGN(c->argv[1]), c->argvlen[1], &rlrange, &deleted);
	} else if (rangetype == ZRANGE_LEX) {
		retval = rl_zremrangebylex(c->context->db, UNSIGN(c->argv[1]), c->argvlen[1], UNSIGN(c->argv[2]), c->argvlen[2], UNSIGN(c->argv[3]), c->argvlen[3], &deleted);
	} else {
		__rliteSetError(c->context, RLITE_ERR, "Unexpected rangetype");
		goto cleanup;
	}
	RLITE_SERVER_ERR(c, retval);

	c->reply = createLongLongObject(deleted);
cleanup:
	return;
}

void zremrangebyrankCommand(rliteClient *c) {
	zremrangeGenericCommand(c,ZRANGE_RANK);
}

void zremrangebyscoreCommand(rliteClient *c) {
	zremrangeGenericCommand(c,ZRANGE_SCORE);
}

void zremrangebylexCommand(rliteClient *c) {
	zremrangeGenericCommand(c,ZRANGE_LEX);
}

struct rliteCommand rliteCommandTable[] = {
	// {"get",getCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"set",setCommand,-3,"wm",0,NULL,1,1,1,0,0},
	// {"setnx",setnxCommand,3,"wmF",0,NULL,1,1,1,0,0},
	// {"setex",setexCommand,4,"wm",0,NULL,1,1,1,0,0},
	// {"psetex",psetexCommand,4,"wm",0,NULL,1,1,1,0,0},
	// {"append",appendCommand,3,"wm",0,NULL,1,1,1,0,0},
	// {"strlen",strlenCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"del",delCommand,-2,"w",0,NULL,1,-1,1,0,0},
	// {"exists",existsCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"setbit",setbitCommand,4,"wm",0,NULL,1,1,1,0,0},
	// {"getbit",getbitCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"setrange",setrangeCommand,4,"wm",0,NULL,1,1,1,0,0},
	// {"getrange",getrangeCommand,4,"r",0,NULL,1,1,1,0,0},
	// {"substr",getrangeCommand,4,"r",0,NULL,1,1,1,0,0},
	// {"incr",incrCommand,2,"wmF",0,NULL,1,1,1,0,0},
	// {"decr",decrCommand,2,"wmF",0,NULL,1,1,1,0,0},
	// {"mget",mgetCommand,-2,"r",0,NULL,1,-1,1,0,0},
	// {"rpush",rpushCommand,-3,"wmF",0,NULL,1,1,1,0,0},
	// {"lpush",lpushCommand,-3,"wmF",0,NULL,1,1,1,0,0},
	// {"rpushx",rpushxCommand,3,"wmF",0,NULL,1,1,1,0,0},
	// {"lpushx",lpushxCommand,3,"wmF",0,NULL,1,1,1,0,0},
	// {"linsert",linsertCommand,5,"wm",0,NULL,1,1,1,0,0},
	// {"rpop",rpopCommand,2,"wF",0,NULL,1,1,1,0,0},
	// {"lpop",lpopCommand,2,"wF",0,NULL,1,1,1,0,0},
	// {"brpop",brpopCommand,-3,"ws",0,NULL,1,1,1,0,0},
	// {"brpoplpush",brpoplpushCommand,4,"wms",0,NULL,1,2,1,0,0},
	// {"blpop",blpopCommand,-3,"ws",0,NULL,1,-2,1,0,0},
	// {"llen",llenCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"lindex",lindexCommand,3,"r",0,NULL,1,1,1,0,0},
	// {"lset",lsetCommand,4,"wm",0,NULL,1,1,1,0,0},
	// {"lrange",lrangeCommand,4,"r",0,NULL,1,1,1,0,0},
	// {"ltrim",ltrimCommand,4,"w",0,NULL,1,1,1,0,0},
	// {"lrem",lremCommand,4,"w",0,NULL,1,1,1,0,0},
	// {"rpoplpush",rpoplpushCommand,3,"wm",0,NULL,1,2,1,0,0},
	// {"sadd",saddCommand,-3,"wmF",0,NULL,1,1,1,0,0},
	// {"srem",sremCommand,-3,"wF",0,NULL,1,1,1,0,0},
	// {"smove",smoveCommand,4,"wF",0,NULL,1,2,1,0,0},
	// {"sismember",sismemberCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"scard",scardCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"spop",spopCommand,2,"wRsF",0,NULL,1,1,1,0,0},
	// {"srandmember",srandmemberCommand,-2,"rR",0,NULL,1,1,1,0,0},
	// {"sinter",sinterCommand,-2,"rS",0,NULL,1,-1,1,0,0},
	// {"sinterstore",sinterstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
	// {"sunion",sunionCommand,-2,"rS",0,NULL,1,-1,1,0,0},
	// {"sunionstore",sunionstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
	// {"sdiff",sdiffCommand,-2,"rS",0,NULL,1,-1,1,0,0},
	// {"sdiffstore",sdiffstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
	// {"smembers",sinterCommand,2,"rS",0,NULL,1,1,1,0,0},
	// {"sscan",sscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
	{"zadd",zaddCommand,-4,"wmF",0,1,1,1,0,0},
	{"zincrby",zincrbyCommand,4,"wmF",0,1,1,1,0,0},
	{"zrem",zremCommand,-3,"wF",0,1,1,1,0,0},
	{"zremrangebyscore",zremrangebyscoreCommand,4,"w",0,1,1,1,0,0},
	{"zremrangebyrank",zremrangebyrankCommand,4,"w",0,1,1,1,0,0},
	{"zremrangebylex",zremrangebylexCommand,4,"w",0,1,1,1,0,0},
	// {"zunionstore",zunionstoreCommand,-4,"wm",0,zunionInterGetKeys,0,0,0,0,0},
	// {"zinterstore",zinterstoreCommand,-4,"wm",0,zunionInterGetKeys,0,0,0,0,0},
	{"zrange",zrangeCommand,-4,"r",0,1,1,1,0,0},
	// {"zrangebyscore",zrangebyscoreCommand,-4,"r",0,NULL,1,1,1,0,0},
	// {"zrevrangebyscore",zrevrangebyscoreCommand,-4,"r",0,NULL,1,1,1,0,0},
	// {"zrangebylex",zrangebylexCommand,-4,"r",0,NULL,1,1,1,0,0},
	// {"zrevrangebylex",zrevrangebylexCommand,-4,"r",0,NULL,1,1,1,0,0},
	// {"zcount",zcountCommand,4,"rF",0,NULL,1,1,1,0,0},
	// {"zlexcount",zlexcountCommand,4,"rF",0,NULL,1,1,1,0,0},
	{"zrevrange",zrevrangeCommand,-4,"r",0,1,1,1,0,0},
	// {"zcard",zcardCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"zscore",zscoreCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"zrank",zrankCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"zrevrank",zrevrankCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"zscan",zscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
	// {"hset",hsetCommand,4,"wmF",0,NULL,1,1,1,0,0},
	// {"hsetnx",hsetnxCommand,4,"wmF",0,NULL,1,1,1,0,0},
	// {"hget",hgetCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"hmset",hmsetCommand,-4,"wm",0,NULL,1,1,1,0,0},
	// {"hmget",hmgetCommand,-3,"r",0,NULL,1,1,1,0,0},
	// {"hincrby",hincrbyCommand,4,"wmF",0,NULL,1,1,1,0,0},
	// {"hincrbyfloat",hincrbyfloatCommand,4,"wmF",0,NULL,1,1,1,0,0},
	// {"hdel",hdelCommand,-3,"wF",0,NULL,1,1,1,0,0},
	// {"hlen",hlenCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"hkeys",hkeysCommand,2,"rS",0,NULL,1,1,1,0,0},
	// {"hvals",hvalsCommand,2,"rS",0,NULL,1,1,1,0,0},
	// {"hgetall",hgetallCommand,2,"r",0,NULL,1,1,1,0,0},
	// {"hexists",hexistsCommand,3,"rF",0,NULL,1,1,1,0,0},
	// {"hscan",hscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
	// {"incrby",incrbyCommand,3,"wmF",0,NULL,1,1,1,0,0},
	// {"decrby",decrbyCommand,3,"wmF",0,NULL,1,1,1,0,0},
	// {"incrbyfloat",incrbyfloatCommand,3,"wmF",0,NULL,1,1,1,0,0},
	// {"getset",getsetCommand,3,"wm",0,NULL,1,1,1,0,0},
	// {"mset",msetCommand,-3,"wm",0,NULL,1,-1,2,0,0},
	// {"msetnx",msetnxCommand,-3,"wm",0,NULL,1,-1,2,0,0},
	// {"randomkey",randomkeyCommand,1,"rR",0,NULL,0,0,0,0,0},
	// {"select",selectCommand,2,"rlF",0,NULL,0,0,0,0,0},
	// {"move",moveCommand,3,"wF",0,NULL,1,1,1,0,0},
	// {"rename",renameCommand,3,"w",0,NULL,1,2,1,0,0},
	// {"renamenx",renamenxCommand,3,"wF",0,NULL,1,2,1,0,0},
	// {"expire",expireCommand,3,"wF",0,NULL,1,1,1,0,0},
	// {"expireat",expireatCommand,3,"wF",0,NULL,1,1,1,0,0},
	// {"pexpire",pexpireCommand,3,"wF",0,NULL,1,1,1,0,0},
	// {"pexpireat",pexpireatCommand,3,"wF",0,NULL,1,1,1,0,0},
	// {"keys",keysCommand,2,"rS",0,NULL,0,0,0,0,0},
	// {"scan",scanCommand,-2,"rR",0,NULL,0,0,0,0,0},
	// {"dbsize",dbsizeCommand,1,"rF",0,NULL,0,0,0,0,0},
	// {"auth",authCommand,2,"rsltF",0,NULL,0,0,0,0,0},
	{"ping",pingCommand,-1,"rtF",0,0,0,0,0,0},
	{"echo",echoCommand,2,"rF",0,0,0,0,0,0},
	// {"save",saveCommand,1,"ars",0,NULL,0,0,0,0,0},
	// {"bgsave",bgsaveCommand,1,"ar",0,NULL,0,0,0,0,0},
	// {"bgrewriteaof",bgrewriteaofCommand,1,"ar",0,NULL,0,0,0,0,0},
	// {"shutdown",shutdownCommand,-1,"arlt",0,NULL,0,0,0,0,0},
	// {"lastsave",lastsaveCommand,1,"rRF",0,NULL,0,0,0,0,0},
	// {"type",typeCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"multi",multiCommand,1,"rsF",0,NULL,0,0,0,0,0},
	// {"exec",execCommand,1,"sM",0,NULL,0,0,0,0,0},
	// {"discard",discardCommand,1,"rsF",0,NULL,0,0,0,0,0},
	// {"sync",syncCommand,1,"ars",0,NULL,0,0,0,0,0},
	// {"psync",syncCommand,3,"ars",0,NULL,0,0,0,0,0},
	// {"replconf",replconfCommand,-1,"arslt",0,NULL,0,0,0,0,0},
	// {"flushdb",flushdbCommand,1,"w",0,NULL,0,0,0,0,0},
	// {"flushall",flushallCommand,1,"w",0,NULL,0,0,0,0,0},
	// {"sort",sortCommand,-2,"wm",0,sortGetKeys,1,1,1,0,0},
	// {"info",infoCommand,-1,"rlt",0,NULL,0,0,0,0,0},
	// {"monitor",monitorCommand,1,"ars",0,NULL,0,0,0,0,0},
	// {"ttl",ttlCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"pttl",pttlCommand,2,"rF",0,NULL,1,1,1,0,0},
	// {"persist",persistCommand,2,"wF",0,NULL,1,1,1,0,0},
	// {"slaveof",slaveofCommand,3,"ast",0,NULL,0,0,0,0,0},
	// {"role",roleCommand,1,"last",0,NULL,0,0,0,0,0},
	// {"debug",debugCommand,-2,"as",0,NULL,0,0,0,0,0},
	// {"config",configCommand,-2,"art",0,NULL,0,0,0,0,0},
	// {"subscribe",subscribeCommand,-2,"rpslt",0,NULL,0,0,0,0,0},
	// {"unsubscribe",unsubscribeCommand,-1,"rpslt",0,NULL,0,0,0,0,0},
	// {"psubscribe",psubscribeCommand,-2,"rpslt",0,NULL,0,0,0,0,0},
	// {"punsubscribe",punsubscribeCommand,-1,"rpslt",0,NULL,0,0,0,0,0},
	// {"publish",publishCommand,3,"pltrF",0,NULL,0,0,0,0,0},
	// {"pubsub",pubsubCommand,-2,"pltrR",0,NULL,0,0,0,0,0},
	// {"watch",watchCommand,-2,"rsF",0,NULL,1,-1,1,0,0},
	// {"unwatch",unwatchCommand,1,"rsF",0,NULL,0,0,0,0,0},
	// {"cluster",clusterCommand,-2,"ar",0,NULL,0,0,0,0,0},
	// {"restore",restoreCommand,-4,"awm",0,NULL,1,1,1,0,0},
	// {"restore-asking",restoreCommand,-4,"awmk",0,NULL,1,1,1,0,0},
	// {"migrate",migrateCommand,-6,"aw",0,NULL,0,0,0,0,0},
	// {"asking",askingCommand,1,"r",0,NULL,0,0,0,0,0},
	// {"readonly",readonlyCommand,1,"rF",0,NULL,0,0,0,0,0},
	// {"readwrite",readwriteCommand,1,"rF",0,NULL,0,0,0,0,0},
	// {"dump",dumpCommand,2,"ar",0,NULL,1,1,1,0,0},
	// {"object",objectCommand,3,"r",0,NULL,2,2,2,0,0},
	// {"client",clientCommand,-2,"ars",0,NULL,0,0,0,0,0},
	// {"eval",evalCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
	// {"evalsha",evalShaCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
	// {"slowlog",slowlogCommand,-2,"r",0,NULL,0,0,0,0,0},
	// {"script",scriptCommand,-2,"ras",0,NULL,0,0,0,0,0},
	// {"time",timeCommand,1,"rRF",0,NULL,0,0,0,0,0},
	// {"bitop",bitopCommand,-4,"wm",0,NULL,2,-1,1,0,0},
	// {"bitcount",bitcountCommand,-2,"r",0,NULL,1,1,1,0,0},
	// {"bitpos",bitposCommand,-3,"r",0,NULL,1,1,1,0,0},
	// {"wait",waitCommand,3,"rs",0,NULL,0,0,0,0,0},
	// {"command",commandCommand,0,"rlt",0,NULL,0,0,0,0,0},
	// {"pfselftest",pfselftestCommand,1,"r",0,NULL,0,0,0,0,0},
	// {"pfadd",pfaddCommand,-2,"wmF",0,NULL,1,1,1,0,0},
	// {"pfcount",pfcountCommand,-2,"w",0,NULL,1,1,1,0,0},
	// {"pfmerge",pfmergeCommand,-2,"wm",0,NULL,1,-1,1,0,0},
	// {"pfdebug",pfdebugCommand,-3,"w",0,NULL,0,0,0,0,0},
	// {"latency",latencyCommand,-2,"arslt",0,NULL,0,0,0,0,0}
};

struct rliteCommand *lookupCommand(const char *name, size_t UNUSED(len)) {
	int j;
	int numcommands = sizeof(rliteCommandTable)/sizeof(struct rliteCommand);

	for (j = 0; j < numcommands; j++) {
		struct rliteCommand *c = rliteCommandTable+j;
		if (strcasecmp(c->name, name) == 0) {
			return c;
		}
	}

	return NULL;
}
