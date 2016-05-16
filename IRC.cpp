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

#include "IRC.hpp"

namespace cpIRC
{
	IRC::IRC(void(*printFunction)(const char* fmt, ...))
	{
		callbackList = 0;
		connected = false;
		prnt = printFunction;
	}

	IRC::~IRC()
	{
		if (connected)
			disconnect();

		clear_callbacks();
	}

	int IRC::connect(const char* server, const short int port)
	{
		hostent* resolve;
		sockaddr_in rem;

		if (connected)
			return IRC_ALREADY_CONNECTED;

		ircSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (ircSocket == INVALID_SOCKET)
			return IRC_SOCKET_CREATION_FAILED;

		resolve = gethostbyname(server);
		if (!resolve)
		{
			closesocket(ircSocket);
			return IRC_RESOLVE_FAILED;
		}

		memcpy(&rem.sin_addr, resolve->h_addr, 4);
		rem.sin_family = AF_INET;
		rem.sin_port = htons(port);

		if (::connect(ircSocket, reinterpret_cast<const sockaddr*>(&rem), sizeof(rem)) == SOCKET_ERROR)
		{
#ifdef WIN32
			if (prnt)
				prnt("[cpIRC]: Failed to connect: %d\n", WSAGetLastError());
#endif
			closesocket(ircSocket);
			return IRC_SOCKET_CONNECT_FAILED;
		}

		connected = true;
		return IRC_SUCCESS;
	}

	void IRC::set_callback(const char* cmd, int(*function_ptr)(IRC*, IRCReply*))
	{
		if (!callbackList)
		{
			callbackList = new CallbackHandler;
			callbackList->next = NULL;
			callbackList->callback = function_ptr;

			unsigned int cmd_length = strlen(cmd) + 1;
			callbackList->command = new char[cmd_length]();
			irc_strcpy(callbackList->command, cmd_length + 1, cmd);

			return;
		}

		CallbackHandler* last = callbackList;
		while (last->next)
			last = last->next;

		last->next = new CallbackHandler;
		last->next->next = NULL;
		last->next->callback = function_ptr;

		unsigned int cmd_length = strlen(cmd) + 1;
		last->next->command = new char[cmd_length]();
		irc_strcpy(last->next->command, cmd_length + 1, cmd);
	}

	int IRC::message_loop()
	{
		if (!connected)
			return IRC_NOT_CONNECTED;

		char* buffer = new char[1024]();

		while (1)
		{
			int ret_len = recv(ircSocket, buffer, 1023, 0);

			if (!ret_len) // Socked has been closed.
				break;

			if (ret_len == SOCKET_ERROR)
			{
#ifdef WIN32
				if (prnt)
					prnt("[cpIRC]: Recv error: %d", WSAGetLastError());
#endif
				delete[] buffer;
				return IRC_RECV_FAILED;
			}

			buffer[ret_len] = '\0';
			split_to_replies(buffer);
		}

		delete[] buffer;
		return IRC_SUCCESS;
	}

	int IRC::disconnect()
	{
		if (!connected)
			return IRC_NOT_CONNECTED;

		if (quit("Leaving") != IRC_SUCCESS)
			return IRC_SEND_FAILED;

		if (shutdown(ircSocket, 2))
		{
#ifdef WIN32
			prnt("[cpIRC]: Socket shutdown error. Last WSA error: %d\n", WSAGetLastError());
#endif
			return IRC_SOCKET_SHUTDOWN_FAILED;
		}

		if (closesocket(ircSocket))
			return IRC_SOCKET_CLOSE_FAILED;

		connected = false;
		return IRC_SUCCESS;
	}

	int IRC::raw(const char* text)
	{
		return irc_send("%s\r\n", text);
	}

	int IRC::pass(const char* password)
	{
		return irc_send("PASS %s\r\n", password);
	}

	int IRC::nick(const char* nickname)
	{
		return irc_send("NICK %s\r\n", nickname);
	}

	int IRC::user(const char* username, const char* hostname, const char* servername, const char* realname)
	{
		return irc_send("USER %s %s %s :%s\r\n", username, hostname, servername, realname);
	}

	int IRC::quit()
	{
		return irc_send("QUIT\r\n");
	}

	int IRC::quit(const char* quit_message)
	{
		return irc_send("QUIT %s\r\n", quit_message);
	}

	int IRC::oper(const char* user, const char* password)
	{
		return irc_send("OPER %s %s\r\n", user, password);
	}

	int IRC::join(const char* channels)
	{
		return irc_send("JOIN %s\r\n", channels);
	}

	int IRC::join(const char* channels, const char* keys)
	{
		return irc_send("JOIN %s %s\r\n", channels, keys);
	}

	int IRC::part(const char* channels)
	{
		return irc_send("PART %s\r\n", channels);
	}

	int IRC::mode(const char* nickname, const char* modes)
	{
		return irc_send("MODE %s %s\r\n", nickname, modes);
	}

	int IRC::mode(const char* channel, const char* modes, const char* limit, const char* user, const char* banmask)
	{
		int result;

		if (limit)
		{
			if (user)
			{
				if (banmask)
					result = irc_send("MODE %s %s\r\n", channel, modes, limit, user, banmask);
				else
					result = irc_send("MODE %s %s\r\n", channel, modes, limit, user);
			}
			else if (banmask)
				result = irc_send("MODE %s %s %s %s\r\n", channel, modes, limit, banmask);
			else
				result = irc_send("MODE %s %s %s\r\n", channel, modes, limit);
		}
		else if (user)
		{
			if (banmask)
				result = irc_send("MODE %s %s %s %s\r\n", channel, modes, user, banmask);
			else
				result = irc_send("MODE %s %s %s\r\n", channel, modes, user);
		}
		else if (banmask)
			result = irc_send("MODE %s %s %s\r\n", channel, modes, banmask);
		else
			result = irc_send("MODE %s %s\r\n", channel, modes);

		return result;
	}

	int IRC::topic(const char* channel)
	{
		return irc_send("TOPIC %s\r\n", channel);
	}

	int IRC::topic(const char* channel, const char* topic)
	{
		return irc_send("TOPIC %s :%s\r\n", channel, topic);
	}

	int IRC::names()
	{
		return irc_send("NAMES\r\n");
	}

	int IRC::names(const char* channels)
	{
		return irc_send("NAMES %s\r\n", channels);
	}

	int IRC::list()
	{
		return irc_send("LIST\r\n");
	}

	int IRC::list(const char* channels)
	{
		return irc_send("LIST %s\r\n", channels);
	}

	int IRC::list(const char* channels, const char* server)
	{
		return irc_send("LIST %s %s\r\n", channels, server);
	}

	int IRC::invite(const char* nickname, const char* channel)
	{
		return irc_send("INVITE %s %s\r\n", nickname, channel);
	}

	int IRC::kick(const char* channel, const char* user)
	{
		return irc_send("KICK %s %s\r\n", channel, user);
	}

	int IRC::kick(const char* channel, const char* user, const char* comment)
	{
		return irc_send("KICK %s %s :%s\r\n", channel, user, comment);
	}

	int IRC::privmsg(const char* receiver, const char* text)
	{
		return irc_send("PRIVMSG %s :%s\r\n", receiver, text);
	}

	int IRC::notice(const char* nickname, const char* text)
	{
		return irc_send("NOTICE %s :%s\r\n", nickname, text);
	}

	int IRC::who(const char* name, bool operators)
	{
		int result;
		if (operators)
			result = irc_send("WHO %s o\r\n", name);
		else
			result = irc_send("WHO %s\r\n", name);
		return result;
	}

	int IRC::whois(const char* nickmasks)
	{
		return irc_send("WHOIS %s\r\n", nickmasks);
	}

	int IRC::whois(const char* server, const char* nickmasks)
	{
		return irc_send("WHOIS %s %s\r\n", server, nickmasks);
	}

	int IRC::whowas(const char* nickname)
	{
		return irc_send("WHOWAS %s\r\n", nickname);
	}

	int IRC::whowas(const char* nickname, const int count)
	{
		return irc_send("WHOWAS %s %d\r\n", nickname, count);
	}

	int IRC::whowas(const char* nickname, const int count, const char* server)
	{
		return irc_send("WHOWAS %s %d %s\r\n", nickname, count, server);
	}

	int IRC::kill(const char* nickname, const char* comment)
	{
		return irc_send("KILL %s %s", nickname, comment);
	}

	int IRC::pong(const char* daemon)
	{
		return irc_send("PONG %s\r\n", daemon);
	}

	int IRC::pong(const char* daemon1, const char* daemon2)
	{
		return irc_send("PONG %s %s\r\n", daemon1, daemon2);
	}

	int IRC::away()
	{
		return irc_send("AWAY\r\n");
	}

	int IRC::away(const char* message)
	{
		return irc_send("AWAY :%s\r\n", message);
	}

	int IRC::rehash()
	{
		return irc_send("REHASH\r\n");
	}

	int IRC::restart()
	{
		return irc_send("RESTART\r\n");
	}

	int IRC::summon(const char* user)
	{
		return irc_send("SUMMON %s\r\n", user);
	}

	int IRC::summon(const char* user, const char* server)
	{
		return irc_send("SUMMON %s %s\r\n", user, server);
	}

	int IRC::users()
	{
		return irc_send("USERS\r\n");
	}

	int IRC::users(const char* server)
	{
		return irc_send("USERS %s\r\n", server);
	}

	int IRC::wallops(const char* text)
	{
		return irc_send("WALLOPS :%s\r\n", text);
	}

	int IRC::userhost(const char* nicknames)
	{
		return irc_send("USERHOST %s\r\n", nicknames);
	}

	int IRC::ison(const char* nicknames)
	{
		return irc_send("ISON %s\r\n", nicknames);
	}

	////////////////////
	////////////////////
	///////private//////
	////////////////////
	////////////////////

	void IRC::callback(IRCReply* reply)
	{
		if (!callbackList)
			return;

		CallbackHandler* p = callbackList;

		while (p)
		{
			if (!strcmp(p->command, reply->command))
			{
				(*(p->callback))(this, reply);
				break;
			}
			p = p->next;
		}
	}

	void IRC::parse_irc_reply(char* message)
	{
		IRCReply reply = { NULL };
		if (prnt)
			prnt("C<-S| %s\n", message);

		char* pointer = message;
		if (message[0] == ':') // Prefix exists.
		{
			reply.nick = ++pointer;
			pointer = strchr(pointer, '!');
			if (!pointer)
			{
				pointer = reply.nick;
				goto done;
			}
			*pointer = '\0';
			reply.user = ++pointer;
			pointer = strchr(pointer, '@');
			if (!pointer)
			{
				pointer = reply.nick;
				goto done;
			}
			*pointer = '\0';
			reply.host = ++pointer;
		done:
			pointer = strchr(pointer, ' ');
			if (!pointer) // No space before command? Bad packet.
				return;
			*pointer = '\0';
			++pointer;
		}
		reply.command = pointer;
		pointer = strchr(pointer, ' ');
		if (pointer) // Parameter list exist.
		{
			*pointer = '\0';
			reply.params = ++pointer;
		}

#ifdef __IRC_DEBUG__
		if (prnt)
			prnt("\tnick\t= %s\n\tuser\t= %s\n\thost\t= %s\n\tcommand\t= %s\n\tparams\t= %s\n", reply.nick, reply.user, reply.host, reply.command, reply.params);
#endif
		
		if (!strcmp(reply.command, "PING"))
		{
			if (!reply.params)
				return;

			irc_send("PONG %s\r\n", &reply.params[1]);

#ifdef __IRC_DEBUG__
			if (prnt)
				prnt("Ping-Pong\n");
#endif
		}
		else
			callback(&reply);
	}

	void IRC::split_to_replies(char* data)
	{
		char* p = strstr(data, "\r\n");
		while (p)
		{
			*p = '\0';
			parse_irc_reply(data);
			data = p + 2;
			p = strstr(data, "\r\n");
		}
	}

	void IRC::clear_callbacks()
	{
		while (callbackList)
		{
			CallbackHandler* iter = callbackList;
			callbackList = callbackList->next;

			delete[] iter->command;
			delete iter;
		}

		callbackList = NULL;
	}

	void IRC::irc_strcpy(char* dest, const unsigned int destLen, const char* src)
	{
#ifdef WIN32
		memcpy_s(dest, destLen, src, min(strlen(src), destLen));
#else
		memcpy(dest, src, min(strlen(src), destLen));
#endif

	}

	int IRC::irc_send(const char* format, ...)
	{
		if (!connected)
			return IRC_NOT_CONNECTED;

		char buffer[512];
		va_list va;
		int result;

		va_start(va, format);
		vsnprintf(buffer, 512, format, va);
		buffer[511] = '\0';
		va_end(va);

		result = send(ircSocket, buffer, min(strlen(buffer), 512), 0) ? IRC_SUCCESS : IRC_SEND_FAILED;

		if (result == IRC_SUCCESS)
		{
			if (!strncmp(buffer, "PASS", 4))
			{
				char* pointer = buffer + 5;
				while (*pointer != '\r')
				{
					*pointer = '*';
					++pointer;
				}
			}
			if (prnt)
				prnt("C->S| %s", buffer);
		}
		return result;
	}
}
