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

namespace cpIRC
{
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

	class IRC
	{
	public:
		IRC(void(*printFunction)(const char* fmt, ...));
		~IRC();

		// Connection registration.

		int pass(char* password);
		int nick(char* nickname);
		int user(char* username, char* hostname, char* servername, char* realname);
		int quit();
		int quit(char* quit_message);
		int oper(char* user, char* password);		

		// Channel operations.

		int join(char* channels);
		int join(char* channels, char* keys);
		int part(char* channels);
		int mode(char* nickname, char* modes);
		int mode(char* channel, char* modes, char* limit, char* user, char* banmask);
		int topic(char* channel);
		int topic(char* channel, char* topic);
		int names();
		int names(char* channels);
		int list();
		int list(char* channels);
		int list(char* channels, char* server);
		int invite(char* nickname, char* channel);
		int kick(char* channel, char* user);
		int kick(char* channel, char* user, char* comment);

		// Sending messages.

		int privmsg(char* receiver, char* text);
		int notice(char* nickname, char* text);

		// User-based queries.

		int who(char* name, bool operators);
		int whois(char* nickmasks);
		int whois(char* server, char* nickmasks);
		int whowas(char* nickname);
		int whowas(char* nickname, int count);
		int whowas(char* nickname, int count, char* server);

		// Miscellaneous messages.

		int kill(char* nickname, char* comment);
		int pong(char* daemon);
		int pong(char* daemon1, char* daemon2);

		// Optional.

		int away();
		int away(char* message);
		int rehash();
		int restart();
		int summon(char* user);
		int summon(char* user, char* server);
		int users();
		int users(char* server);
		int wallops(char* text);
		int userhost(char* nicknames);
		int ison(char* nicknames);

		// This class only.

		int raw(char* text);
		int connect(char* server, short int port);
		void set_callback(char* cmd_name, int(*function_ptr)(char*, irc_reply_data*, IRC*));
		int message_loop();
		int disconnect();

	private:
		struct irc_command_hook;
		struct channel_user;

		void callback(char* irc_command, char*params, irc_reply_data* hostd);
		void parse_irc_reply(char* data);
		void split_to_replies(char* data);
		void clear_irc_command_hook();
		void irc_strcpy_s(char* dest, const unsigned int destLen, char* src);
		int irc_send(const char* format, ...);

#ifndef WIN32
		FILE* dataout;
		FILE* datain;
#endif
		int irc_socket;
		bool connected;
		channel_user* chan_users;
		irc_command_hook* hooks;
		void(*prnt)(const char* format, ...);
	};

	struct IRC::irc_command_hook
	{
		char* irc_command;
		int(*function)(char*, irc_reply_data*, IRC*);
		irc_command_hook* next;
	};

	struct IRC::channel_user
	{
		char* nick;
		char* channel;
		char flags;
		channel_user* next;
	};
}
