/*
	cpIRC - C++ class based IRC protocol wrapper
	Copyright (C) 2003 Iain Sheppard

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	Contacting the author:
	~~~~~~~~~~~~~~~~~~~~~~

	email:	iainsheppard@yahoo.co.uk
	IRC:	#magpie @ irc.quakenet.org
*/
//TODO: save current channel
#include <stdio.h>
#include <stdarg.h>

#ifdef WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")
#include <Windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define closesocket(s) close(s)
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#endif

#define __CPIRC_VERSION__	0.1
#define __IRC_DEBUG__ 1

enum IrcUserFlags
{
	IRC_USER_REGULAR = 0,
	IRC_USER_VOICE = 1,
	IRC_USER_HALFOP = 2,
	IRC_USER_OP = 4
};

enum IrcReturnCodes
{
	IRC_SUCCESS = 0,
	IRC_ALREADY_CONNECTED,
	IRC_NOT_CONNECTED,
	IRC_SOCKET_CREATION_FAILED,
	IRC_RESOLVE_FAILED,
	IRC_SOCKET_CONNECT_FAILED,
	IRC_DATASTREAM_OPEN_FAILED,
	IRC_DATASTREAM_WRITE_FAILED,
	IRC_DATASTREAM_CLOSE_FAILED,
	IRC_SEND_FAILED,
	IRC_RECV_FAILED,
	IRC_SOCKET_SHUTDOWN_FAILED,
	IRC_SOCKET_CLOSE_FAILED
};

struct irc_reply_data
{
	char* nick;
	char* ident;
	char* host;
	char* target;
};

struct irc_command_hook
{
	char* irc_command;
	int (*function)(char*, irc_reply_data*, void*);
	irc_command_hook* next;
};

struct channel_user
{
	char* nick;
	char* channel;
	char flags;
	channel_user* next;
};

class IRC
{
public:
	IRC(void (*printFunction)(const char* fmt, ...));
	~IRC();
	int start(char* server, int port, char* nick, char* user, char* name, char* pass);
	int disconnect();
	int privmsg(char* message);
	int privmsg(const char* format, ...);
	int notice(char* channel, char* message);
	int notice(char* channel, const char* format, ...);
	int join(char* channel);
	int part(char* channel);
	int kick(char* channel, char* nick);
	int kick(char* channel, char* nick, char* message);
	int mode(char* modes);
	int mode(char* channel, char* modes, char* targets);
	int nick(char* newnick);
	int quit(char* quit_message);
	int raw(char* data);
	void hook_irc_command(char* cmd_name, int (*function_ptr)(char*, irc_reply_data*, void*));
	int message_loop();
	bool is_op(char* channel, char* nick);
	bool is_voice(char* channel, char* nick);
	char* current_nick();

private:
	void call_hook(char* irc_command, char*params, irc_reply_data* hostd);
	void parse_irc_reply(char* data);
	void split_to_replies(char* data);
	void clear_irc_command_hook();
	void irc_strcpy_s(char* dest, const unsigned int destLen, char* src);
	int irc_send(const char* format, ...);

	int irc_socket;
	bool connected;
	bool sentnick;
	bool sentpass;
	bool sentuser;
	char* cur_nick;
	FILE* dataout;
	FILE* datain;
	channel_user* chan_users;
	irc_command_hook* hooks;
	void(*prnt)(const char* format, ...) = NULL;
};
