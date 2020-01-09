#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <ctype.h>
#include <string>
#include <unistd.h>
#include <err.h>
#include <sysexits.h>
#include <cassert>

using namespace rapidjson;
using namespace std;

// tokenize a string at the first instance of a separator.
// named in honour of pythons str.partition.
char *
strpart(char *s, const char *sep, char **lasts)
{
	char *pos = strstr(s, sep);

	if (pos) {
		*pos = '\0';
		*lasts = pos + strlen(sep);
	}
	else {
		*lasts = s + strlen(s);
	}

	return s;
}

char *
strtok_r(char *str, int c, char **lasts)
{
	char sep[2] = {static_cast<char>(c & 0xFF), '\0'};
	return strtok_r(str, sep, lasts);
}

static bool
is_number(const char *s)
{
	if (!s[0])
		return false;

	for (; *s; s++) {
		if (strchr(", ", *s))
			return true;

		if (!isdigit(*s))
			return false;
	}

	return true;
}

// like strtok, but returning the delimeter found in sep
static char *
strdelim(char *s, const char *delim, int *sep, char **lasts)
{
	char *end = s + strcspn(s, delim);

	*sep = static_cast<int>(*end);
	*lasts = *end ? end + 1 : end;
	*end = '\0';
	return s;
}

static char *
strtok_datetime(char *s, int *sep, char **lasts)
{
	if (strstr(s, "now-ish ") == s)
		s += strlen("now-ish "); // very cute apple

	char *end = strchr(s, ' ');

	if (end)
		end += strspn(end, " ");
	else {
		warnx("unable to find midpoint while parsing datetime %s", s);
		end = s;
	}

	(void)strdelim(end, " ,)}", sep, lasts);

	return s;
}

static const char*
shortstr(const char *s)
{
	static char buf[128];
	strncpy(buf, s, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] -= '\0';
	return buf;
}

static char *
eat(char *s, const char *eatchars)
{
	while (s[0] && strchr(eatchars, s[0]))
		s++;
	return s;
}


static int
parse_value(Writer<StringBuffer> &writer, char **remainptr)
{
#define remain (*remainptr)

	remain += strspn(remain, " \t\n");

	int c = remain[0];
	int sep = -1;

	if (c == '}' || c == ')')
		return 0;

	if (c == '{') {
		writer.StartObject();
		remain++;

		for (int i=0; *remain; i++) {
			if (i == 200)
				errx(EX_SOFTWARE, "neverending parse at %d!", __LINE__);

			remain += strspn(remain, " \t\n");

			// if the object is empty then we see the finish immediately
			if (remain[0] == '}') {
				writer.EndObject();
				remain++;
				break;
			}

			if (!remain)
				break;

			if (parse_value(writer, &remain))
				return -1;

			if (remain[0] != '=') {
				warnx("%d: unexpected content following key: %s", __LINE__, remain);
				return -1;
			}

			remain++;

			if (parse_value(writer, &remain))
				return -1;

			remain += strspn(remain, " \t\n");

			if (remain[0] == '}') {
				writer.EndObject();
				remain++;
				break;
			}

			if (!remain[0])
				break;

			if (remain[0] != ',') {
				warnx("%d: unexpected content following value, expected , but found '%s'", 
					__LINE__, remain);
				return -1;
			}

			remain++;
		}
	}
	else if (c == '(') {
		writer.StartArray();
		remain++;

		for (int i=0; *remain; i++) {
			if (i == 200)
				errx(EX_SOFTWARE, "neverending parse at %d!", __LINE__);

			remain += strspn(remain, " \t\n");

			if (remain[0] == ')') {
				writer.EndArray();
				remain++;
				break;
			}

			if (!remain)
				break;

			if (parse_value(writer, &remain))
				return -1;

			remain += strspn(remain, " \t\n");

			if (remain[0] == ')') {
				writer.EndArray();
				remain++;
				break;
			}

			if (!remain[0])
				break;

			if (remain[0] != ',') {
				warnx("%d: unexpected content following value, expected , but found '%s'", 
					__LINE__, remain);
				return -1;
			}

			remain++;
		}
	}
	else if (c == '"') {
		char *s = strtok_r(remain + 1, "\"", &remain);
		writer.String(s);
	}
	else if (c == '$') {
		char *s = strdelim(remain+1, ", \t)}", &sep, &remain);
		writer.String(s);
	}
	else if (isdigit(remain[0])) {
		long num = strtol(remain, &remain, 0);
		writer.Int(num);
	}
	else if (strstr(remain, "ASN:") == remain) {
		char *s = strtok_r(remain + 4, ":", &remain);
		writer.String(s);
	}
	else if (strstr(remain, "now-ish") == remain // very cute apple
		||(remain[0] == '2' && remain[1] == '0' 
				&& isdigit(remain[2]) && isdigit(remain[3]) && remain[4] == '/')) {
		char *s = strtok_datetime(remain, &sep, &remain);
		writer.String(s);		
	}
	else {
		char buf[16];
		char *end;

		// copy out just the first token for comparison purposes

		strncpy(buf, remain, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		end = buf + strcspn(buf, " ,)}");
		*end = '\0';

		size_t len = strlen(buf);

		if (len == 0) {
		}
		else if (!strcmp(buf, "true")) {
			writer.Bool(true);
			remain += len;
		}
		else if (!strcmp(buf, "false")) {
			writer.Bool(false);
			remain += len;
		}
		else {
			errx(EX_SOFTWARE, "%d: unrecognized value '%s' - date?", __LINE__, buf);				
		}
	}

	if (sep == ')' || sep == '}' || sep == ',') {
		remain--;
		*remain = sep;
	}


	return 0;
#undef remain	
}

static int
parse_input(istream &in)
{
	string line;
	int error;

	while (getline(in, line)) {
		char *data, *tok, *remain, *s, *s2;

		if (!(data = strdup(line.c_str())))
			err(EX_OSERR, "out of memory!");

		// take the second word 
		strtok_r(data, " ", &remain);

    StringBuffer sb;
    Writer<StringBuffer> writer(sb);

		const char *name = strtok_r(NULL, " ", &remain);

		writer.StartObject();

		writer.Key("name");
		writer.String(name);

		writer.Key("data");

		// find the start of the object
		(void)strpart(remain, "dataRef=", &remain);

		if (!(error = parse_value(writer, &remain))) {
			writer.EndObject();
			assert(writer.IsComplete());
			cout << sb.GetString() << endl;
		}

		free(data);
	}

	return 0;
}


int 
main(int argc, char const *argv[])
{

	if (argc == 1 && !isatty(STDIN_FILENO))
		return parse_input(cin);

	for (int i=1; i<argc; i++) {
		if (argv[i][0] == '-') {
			printf("usage: %s [lsappinfo.log]\n", getprogname());
			return EXIT_FAILURE;
		}
	}

	for (int i=1; i<argc; i++) {
		ifstream in(argv[i]);
		int rc = parse_input(in);
		in.close();

		if (rc)
			return EXIT_FAILURE;
	}

	return 0;
}
