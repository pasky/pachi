#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include "chat.h"

#ifndef HAVE_NO_REGEX_SUPPORT
#include <regex.h>

#define DEBUG

#include "debug.h"
#include "random.h"

#define MAX_CHAT_PATTERNS 500

static struct chat {
	double minwin;
	double maxwin;
	char from[20];
	char regex[100];
	char reply[300]; // in printf format with one param (100*winrate)

	regex_t preg;
	bool displayed;
	bool match;
} *chat_table;

static char default_reply[] = "I know all those words, but that sentence makes no sense to me";
static char not_playing[] = "I'm winning big without playing";

/* Read the chat file, a sequence of lines of the form:
 * minwin;maxwin;from;regex;reply
 * Set minwin, maxwin to -1.0 2.0 for answers to chat other than winrate.
 * Set from as one space for replies to anyone.
 * Examples:
 *   -1.0;0.3; ;winrate;%.1f%% I'm losing
 *   -1.0;2.0;pasky;^when ;Today
 */
void chat_init(char *chat_file) {
	if (!chat_file) return;
	FILE *f = fopen(chat_file, "r");
	if (!f) {
		perror(chat_file);
		return;
	}
	chat_table = calloc2(MAX_CHAT_PATTERNS, sizeof(*chat_table));
	struct chat *entry = chat_table;
	while (fscanf(f, "%lf;%lf;%20[^;];%100[^;];%300[^\n]\n", &entry->minwin, &entry->maxwin,
		      entry->from, entry->regex, entry->reply ) == 5) {
		if (!strcmp(entry->from, " "))
			entry->from[0] = '\0';
		int err = regcomp(&entry->preg, entry->regex, REG_EXTENDED | REG_ICASE);
		if (err) {
			char msg[200];
			regerror(err, &entry->preg, msg, sizeof(msg));
			fprintf(stderr, "Error compiling %s: %s\n", entry->regex, msg);
		} else {
			entry++;
		}
	}
	if (!feof(f))
		fprintf(stderr, "syntax error around line %u in %s\n", (unsigned)(entry - chat_table), chat_file);
	fclose(f);
	if (DEBUGL(1))
		fprintf(stderr, "Loaded %u chat entries from %s\n", (unsigned)(entry - chat_table), chat_file);
}

void chat_done() {
	if (chat_table) {
		free(chat_table);
		chat_table = NULL;
	}
}

/* Reply to a chat. When not playing, color is S_NONE and all remaining parameters are undefined.
 * If some matching entries have not yet been displayed we pick randomly among them. Otherwise
 * we pick randomly among all matching entries. */
char
*generic_chat(struct board *b, bool opponent, char *from, char *cmd, enum stone color, coord_t move,
	      int playouts, int machines, int threads, double winrate, double extra_komi) {

	static char reply[1024];
	if (!chat_table) {
		if (strncasecmp(cmd, "winrate", 7)) return NULL;
		if (color == S_NONE) return not_playing;

		snprintf(reply, 512, "In %d playouts at %d threads, %s %s can win with %.1f%% probability",
			 playouts, threads, stone2str(color), coord2sstr(move, b), 100*winrate);
		if (fabs(extra_komi) >= 0.5) {
			snprintf(reply + strlen(reply), 510, ", while self-imposing extra komi %.1f", extra_komi);
		}
		strcat(reply, ".");
		return reply;
	}
	int matches = 0;
	int undisplayed = 0;
	for (struct chat *entry = chat_table; entry->regex[0]; entry++) {
		entry->match = false;
		if (color != S_NONE) {
			if (winrate < entry->minwin) continue;
			if (winrate > entry->maxwin) continue;
		}
		if (entry->from[0] && strcmp(entry->from, from)) continue;
		if (regexec(&entry->preg, cmd, 0, NULL, 0)) continue;
		entry->match = true;
		matches++;
		if (!entry->displayed) undisplayed++;
	}
	if (matches == 0) return default_reply;
	int choices = undisplayed > 0 ? undisplayed : matches;
	int index = fast_random(choices);
	for (struct chat *entry = chat_table; entry->regex[0]; entry++) {
		if (!entry->match) continue;
		if (undisplayed > 0 && entry->displayed) continue;
		if (--index < 0) {
			entry->displayed = true;
			snprintf(reply, sizeof(reply), entry->reply, 100*winrate);
			return reply;
		}
	}
	assert(0);
	return NULL;
}
#else
void chat_init(char *chat_file) {}

void chat_done() {}

char
*generic_chat(struct board *b, bool opponent, char *from, char *cmd, enum stone color, coord_t move,
	      int playouts, int machines, int threads, double winrate, double extra_komi) {
	static char reply[1024] = { '.', '\0' };
	return reply;
}
#endif
