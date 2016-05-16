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

#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN64

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")
#include <Windows.h>

#else

#include <unistd.h>
#include <string.h>
#include <netdb.h>
#define closesocket(s) close(s)
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1

#endif

#include "IRC_errors.hpp"
#include "IRC_responses.hpp"

#ifndef min
#define min(a, b) (a < b ? a : b)
#endif

#define __CPIRC_VERSION__	0.1
#define __IRC_DEBUG__ 1

namespace cpIRC
{
	enum IRCReturnCodes
	{
		IRC_SUCCESS = 0,
		IRC_ALREADY_CONNECTED,
		IRC_NOT_CONNECTED,
		IRC_SOCKET_CREATION_FAILED,
		IRC_RESOLVE_FAILED,
		IRC_SOCKET_CONNECT_FAILED,
		IRC_SEND_FAILED,
		IRC_RECV_FAILED,
		IRC_SOCKET_SHUTDOWN_FAILED,
		IRC_SOCKET_CLOSE_FAILED
	};

	struct IRCReply
	{
		// Prefix.
		char* nick;
		char* user;
		char* host;
		// Command.
		char* command;
		// Params.
		char* params;
	};

	class IRC
	{
	public:
		IRC(void(*printFunction)(const char* fmt, ...));
		~IRC();

		// This class only.

		int connect(const char* server, const short int port);
		void set_callback(const char* cmd, int(*function_ptr)(IRC*, IRCReply*));
		int message_loop();
		int disconnect();
		int raw(const char* text);

		// Connection registration.

		int pass(const char* password);
		int nick(const char* nickname);
		int user(const char* username, const char* hostname, const char* servername, const char* realname);
		int quit();
		int quit(const char* quit_message);
		int oper(const char* user, const char* password);

		// Channel operations.

		int join(const char* channels);
		int join(const char* channels, const char* keys);
		int part(const char* channels);
		int mode(const char* nickname, const char* modes);
		int mode(const char* channel, const char* modes, const char* limit, const char* user, const char* banmask);
		int topic(const char* channel);
		int topic(const char* channel, const char* topic);
		int names();
		int names(const char* channels);
		int list();
		int list(const char* channels);
		int list(const char* channels, const char* server);
		int invite(const char* nickname, const char* channel);
		int kick(const char* channel, const char* user);
		int kick(const char* channel, const char* user, const char* comment);

		// Sending messages.

		int privmsg(const char* receiver, const char* text);
		int notice(const char* nickname, const char* text);

		// User-based queries.

		int who(const char* name, bool operators);
		int whois(const char* nickmasks);
		int whois(const char* server, const char* nickmasks);
		int whowas(const char* nickname);
		int whowas(const char* nickname, const int count);
		int whowas(const char* nickname, const int count, const char* server);

		// Miscellaneous messages.

		int kill(const char* nickname, const char* comment);
		int pong(const char* daemon);
		int pong(const char* daemon1, const char* daemon2);

		// Optional.

		int away();
		int away(const char* message);
		int rehash();
		int restart();
		int summon(const char* user);
		int summon(const char* user, const char* server);
		int users();
		int users(const char* server);
		int wallops(const char* text);
		int userhost(const char* nicknames);
		int ison(const char* nicknames);

	private:
		struct CallbackHandler;
		struct UserHandler;

		void callback(IRCReply* reply);
		void parse_irc_reply(char* message);
		void split_to_replies(char* data);
		void clear_callbacks();
		void irc_strcpy(char* dest, const unsigned int destLen, const char* src);
		int irc_send(const char* format, ...);

		int ircSocket;
		bool connected;
		CallbackHandler* callbackList;
		void(*prnt)(const char* format, ...);
	};

	struct IRC::CallbackHandler
	{
		char* command;
		int(*callback)(IRC*, IRCReply*);
		CallbackHandler* next;
	};
}
