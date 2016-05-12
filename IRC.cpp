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
		hooks = 0;
		connected = false;
		chan_users = 0;
		prnt = printFunction;
	}

	IRC::~IRC()
	{
		if (connected)
			disconnect();

		clear_irc_command_hook();
	}

	void IRC::set_callback(char* cmd_name, int(*function_ptr)(char*, irc_reply_data*, IRC*))
	{
		if (!hooks)
		{
			hooks = new irc_command_hook;
			hooks->next = NULL;
			hooks->function = function_ptr;

			unsigned int cmd_name_length = strlen(cmd_name) + 1;
			hooks->irc_command = new char[cmd_name_length];
			irc_strcpy_s(hooks->irc_command, cmd_name_length, cmd_name);

			return;
		}

		irc_command_hook* last = hooks;
		while (last->next)
			last = last->next;

		last->next = new irc_command_hook;
		last->next->next = NULL;
		last->next->function = function_ptr;

		unsigned int cmd_name_length = strlen(cmd_name) + 1;
		last->next->irc_command = new char[cmd_name_length];
		irc_strcpy_s(last->next->irc_command, cmd_name_length, cmd_name);
	}

	void IRC::clear_irc_command_hook()
	{
		while (hooks)
		{
			irc_command_hook* iter = hooks;
			hooks = hooks->next;

			delete[] iter->irc_command;
			delete iter;
		}

		hooks = NULL;
	}

	//
	void IRC::irc_strcpy_s(char* dest, const unsigned int destLen, char* src)
	{
#ifdef WIN32
		strcpy_s(dest, destLen, src);
#else
		strcpy(dest, src);
#endif
	}

	//
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

#ifdef WIN32
		result = send(irc_socket, buffer, min(strlen(buffer), 512), 0) ? IRC_SUCCESS : IRC_SEND_FAILED;
#else
		fprintf(dataout, "%s", buffer);
		result = !fflush(dataout) ? IRC_SUCCESS : IRC_DATASTREAM_WRITE_FAILED;
#endif

		return result;
	}

	//
	int IRC::connect(char* server, short int port)
	{
		hostent* resolve;
		sockaddr_in rem;

		if (connected)
			return IRC_ALREADY_CONNECTED;

		irc_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (irc_socket == INVALID_SOCKET)
			return IRC_SOCKET_CREATION_FAILED;

		resolve = gethostbyname(server);
		if (!resolve)
		{
			closesocket(irc_socket);
			return IRC_RESOLVE_FAILED;
		}

		memcpy(&rem.sin_addr, resolve->h_addr, 4);
		rem.sin_family = AF_INET;
		rem.sin_port = htons(port);

		if (::connect(irc_socket, reinterpret_cast<const sockaddr*>(&rem), sizeof(rem)) == SOCKET_ERROR)
		{
#ifdef WIN32
			if(prnt)
				prnt("Failed to connect: %d\n", WSAGetLastError());
#endif
			closesocket(irc_socket);
			return IRC_SOCKET_CONNECT_FAILED;
		}

#ifndef WIN32
		dataout = fdopen(irc_socket, "w");
		//datain=fdopen(irc_socket, "r");
		if (!dataout /*|| !datain*/)
		{
			if(prnt)
				prnt("Failed to open streams!\n");
			closesocket(irc_socket);
			return IRC_DATASTREAM_OPEN_FAILED;
		}
#endif
		connected = true;
		return IRC_SUCCESS;
	}

	int IRC::pass(char* password)
	{
		return irc_send("PASS %s\r\n", password);
	}

	//
	int IRC::disconnect()
	{
		if (!connected)
			return IRC_NOT_CONNECTED;

#ifndef WIN32
		if (fclose(dataout))
			return IRC_DATASTREAM_CLOSE_FAILED;
#endif

		if (quit("Leaving") != IRC_SUCCESS)
			return IRC_SEND_FAILED;

#ifdef WIN32
		if (shutdown(irc_socket, 2))
		{
			prnt("[cpIRC]: Socket shutdown error. Last WSA error: %d\n", WSAGetLastError());
			return IRC_SOCKET_SHUTDOWN_FAILED;
		}
#endif

		if (closesocket(irc_socket))
			return IRC_SOCKET_CLOSE_FAILED;

		connected = false;
		return IRC_SUCCESS;
	}

	int IRC::quit()
	{
		return irc_send("QUIT\r\n");
	}

	int IRC::quit(char* quit_message)
	{
		return irc_send("QUIT %s\r\n", quit_message);
	}

	int IRC::message_loop()
	{
		if (!connected)
			return IRC_NOT_CONNECTED;

		char* buffer = new char[1024];

		while (1)
		{
			int ret_len = recv(irc_socket, buffer, 1023, 0);

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

	//
	void IRC::split_to_replies(char* data)
	{
		char* p;

		while (p = strstr(data, "\r\n"))
		{
			*p = '\0';
			parse_irc_reply(data);
			data = p + 2;
		}
	}

	//-
	void IRC::parse_irc_reply(char* data)
	{
		char* hostd;
		char* cmd;
		char* params;
		irc_reply_data hostd_tmp;
		channel_user* cup;
		char* p;
		char* chan_temp;

		hostd_tmp.target = 0;

		if(prnt)
			prnt("%s\n", data);

		if (data[0] == ':')
		{
			hostd = &data[1];
			cmd = strchr(hostd, ' ');
			if (!cmd)
				return;
			*cmd = '\0';
			cmd++;
			params = strchr(cmd, ' ');
			if (params)
			{
				*params = '\0';
				params++;
			}
			hostd_tmp.nick = hostd;
			hostd_tmp.ident = strchr(hostd, '!');
			if (hostd_tmp.ident)
			{
				*hostd_tmp.ident = '\0';
				hostd_tmp.ident++;
				hostd_tmp.host = strchr(hostd_tmp.ident, '@');
				if (hostd_tmp.host)
				{
					*hostd_tmp.host = '\0';
					hostd_tmp.host++;
				}
			}

			if (!strcmp(cmd, "JOIN"))
			{
				cup = chan_users;
				if (cup)
				{
					while (cup->nick)
					{
						if (!cup->next)
						{
							cup->next = new channel_user;
							cup->next->channel = 0;
							cup->next->flags = 0;
							cup->next->next = 0;
							cup->next->nick = 0;
						}
						cup = cup->next;
					}
					unsigned int params_length = strlen(params) + 1;
					cup->channel = new char[params_length];
					irc_strcpy_s(cup->channel, params_length, params);

					unsigned int hostd_tmp_nick_length = strlen(hostd_tmp.nick) + 1;
					cup->nick = new char[hostd_tmp_nick_length];
					irc_strcpy_s(cup->nick, hostd_tmp_nick_length, hostd_tmp.nick);
				}
			}
			else if (!strcmp(cmd, "PART"))
			{
				channel_user* d;
				channel_user* prev;

				d = 0;
				prev = 0;
				cup = chan_users;
				while (cup)
				{
					if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
					{
						d = cup;
						break;
					}
					else
					{
						prev = cup;
					}
					cup = cup->next;
				}
				if (d)
				{
					if (d == chan_users)
					{
						chan_users = d->next;
						if (d->channel)
							delete[] d->channel;
						if (d->nick)
							delete[] d->nick;
						delete d;
					}
					else
					{
						if (prev)
						{
							prev->next = d->next;
						}
						chan_users = d->next;
						if (d->channel)
							delete[] d->channel;
						if (d->nick)
							delete[] d->nick;
						delete d;
					}
				}
			}
			else if (!strcmp(cmd, "QUIT"))
			{
				channel_user* d;
				channel_user* prev;

				d = 0;
				prev = 0;
				cup = chan_users;
				while (cup)
				{
					if (!strcmp(cup->nick, hostd_tmp.nick))
					{
						d = cup;
						if (d == chan_users)
						{
							chan_users = d->next;
							if (d->channel)
								delete[] d->channel;
							if (d->nick)
								delete[] d->nick;
							delete d;
						}
						else
						{
							if (prev)
							{
								prev->next = d->next;
							}
							if (d->channel)
								delete[] d->channel;
							if (d->nick)
								delete[] d->nick;
							delete d;
						}
						break;
					}
					else
					{
						prev = cup;
					}
					cup = cup->next;
				}
			}
			else if (!strcmp(cmd, "MODE"))
			{
				char* chan;
				char* changevars;
				channel_user* cup;
				channel_user* d;
				char* tmp;
				int i;
				bool plus;

				chan = params;
				params = strchr(chan, ' ');
				*params = '\0';
				params++;
				changevars = params;
				params = strchr(changevars, ' ');
				if (!params)
				{
					return;
				}
				if (chan[0] != '#')
				{
					return;
				}
				*params = '\0';
				params++;

				plus = false;
				for (i = 0; i < (signed)strlen(changevars); i++)
				{
					switch (changevars[i])
					{
					case '+':
						plus = true;
						break;
					case '-':
						plus = false;
						break;
					case 'o':
						tmp = strchr(params, ' ');
						if (tmp)
						{
							*tmp = '\0';
							tmp++;
						}
						tmp = params;
						if (plus)
						{
							// user has been opped (chan, params)
							cup = chan_users;
							d = 0;
							while (cup)
							{
								if (cup->next && cup->channel)
								{
									if (!strcmp(cup->channel, chan) && !strcmp(cup->nick, tmp))
									{
										d = cup;
										break;
									}
								}
								cup = cup->next;
							}
							if (d)
							{
								d->flags = d->flags | IRC_USER_OP;
							}
						}
						else
						{
							// user has been deopped (chan, params)
							cup = chan_users;
							d = 0;
							while (cup)
							{
								if (!strcmp(cup->channel, chan) && !strcmp(cup->nick, tmp))
								{
									d = cup;
									break;
								}
								cup = cup->next;
							}
							if (d)
							{
								d->flags = d->flags^IRC_USER_OP;
							}
						}
						params = tmp;
						break;
					case 'v':
						tmp = strchr(params, ' ');
						if (tmp)
						{
							*tmp = '\0';
							tmp++;
						}
						if (plus)
						{
							// user has been voiced
							cup = chan_users;
							d = 0;
							while (cup)
							{
								if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
								{
									d = cup;
									break;
								}
								cup = cup->next;
							}
							if (d)
							{
								d->flags = d->flags | IRC_USER_VOICE;
							}
						}
						else
						{
							// user has been devoiced
							cup = chan_users;
							d = 0;
							while (cup)
							{
								if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
								{
									d = cup;
									break;
								}
								cup = cup->next;
							}
							if (d)
							{
								d->flags = d->flags^IRC_USER_VOICE;
							}
						}
						params = tmp;
						break;
					default:
						return;
						break;
					}
					// ------------ END OF MODE ---------------
				}
			}
			else if (!strcmp(cmd, "353"))
			{
				// receiving channel names list
				if (!chan_users)
				{
					chan_users = new channel_user;
					chan_users->next = 0;
					chan_users->nick = 0;
					chan_users->flags = 0;
					chan_users->channel = 0;
				}
				cup = chan_users;
				chan_temp = strchr(params, '#');
				if (chan_temp)
				{
					//chan_temp+=3;
					p = strstr(chan_temp, " :");
					if (p)
					{
						*p = '\0';
						p += 2;
						while (strchr(p, ' '))
						{
							char* tmp;

							tmp = strchr(p, ' ');
							*tmp = '\0';
							tmp++;
							while (cup->nick)
							{
								if (!cup->next)
								{
									cup->next = new channel_user;
									cup->next->channel = 0;
									cup->next->flags = 0;
									cup->next->next = 0;
									cup->next->nick = 0;
								}
								cup = cup->next;
							}
							if (p[0] == '@')
							{
								cup->flags = cup->flags | IRC_USER_OP;
								p++;
							}
							else if (p[0] == '+')
							{
								cup->flags = cup->flags | IRC_USER_VOICE;
								p++;
							}
							unsigned int p_length = strlen(p) + 1;
							cup->nick = new char[p_length];
							irc_strcpy_s(cup->nick, p_length, p);

							unsigned int chan_temp_length = strlen(p) + 1;
							cup->channel = new char[chan_temp_length];
							irc_strcpy_s(cup->channel, chan_temp_length, chan_temp);

							p = tmp;
						}
						while (cup->nick)
						{
							if (!cup->next)
							{
								cup->next = new channel_user;
								cup->next->channel = 0;
								cup->next->flags = 0;
								cup->next->next = 0;
								cup->next->nick = 0;
							}
							cup = cup->next;
						}
						if (p[0] == '@')
						{
							cup->flags = cup->flags | IRC_USER_OP;
							p++;
						}
						else if (p[0] == '+')
						{
							cup->flags = cup->flags | IRC_USER_VOICE;
							p++;
						}
						unsigned int p_length = strlen(p) + 1;
						cup->nick = new char[p_length];
						irc_strcpy_s(cup->nick, p_length, p);

						unsigned int chan_temp_length = strlen(chan_temp) + 1;
						cup->channel = new char[chan_temp_length];
						irc_strcpy_s(cup->channel, chan_temp_length, chan_temp);
					}
				}
			}
			else if (!strcmp(cmd, "NOTICE"))
			{
				hostd_tmp.target = params;
				params = strchr(hostd_tmp.target, ' ');
				if (params)
					*params = '\0';
				params++;
#ifdef __IRC_DEBUG__
				prnt("%s >-%s- %s\n", hostd_tmp.nick, hostd_tmp.target, &params[1]);
#endif
			}
			else if (!strcmp(cmd, "PRIVMSG"))
			{
				hostd_tmp.target = params;
				params = strchr(hostd_tmp.target, ' ');
				if (!params)
					return;
				*(params++) = '\0';
#ifdef __IRC_DEBUG__
				prnt("%s: <%s> %s\n", hostd_tmp.target, hostd_tmp.nick, &params[1]);
#endif
			}
			/*else if (!strcmp(cmd, "NICK"))
			{
				if (!strcmp(hostd_tmp.nick, cur_nick))
				{
					delete[] cur_nick;
					unsigned int cur_nick_length = strlen(params) + 1;
					cur_nick = new char[cur_nick_length];
					irc_strcpy_s(cur_nick, cur_nick_length, params);
				}
			}*/
			/* else if (!strcmp(cmd, ""))
			{
				#ifdef __IRC_DEBUG__
				#endif
			} */
			call_hook(cmd, params, &hostd_tmp);
		}
		else
		{
			cmd = data;
			data = strchr(cmd, ' ');
			if (!data)
				return;
			*data = '\0';
			params = data + 1;

			if (!strcmp(cmd, "PING"))
			{
				if (!params)
					return;
				irc_send("PONG %s\r\n", &params[1]);
#ifdef __IRC_DEBUG__
				if(prnt)
					prnt("Ping received, pong sent.\n");
#endif
			}
			else
			{
				hostd_tmp.host = 0;
				hostd_tmp.ident = 0;
				hostd_tmp.nick = 0;
				hostd_tmp.target = 0;
				callback(cmd, params, &hostd_tmp);
			}
		}
	}
	//
	void IRC::callback(char* irc_command, char* params, irc_reply_data* hostd)
	{
		if (!hooks)
			return;

		irc_command_hook* p = hooks;

		while (p)
		{
			if (!strcmp(p->irc_command, irc_command))
			{
				(*(p->function))(params, hostd, this);
				break;
			}
			p = p->next;
		}
	}

	int IRC::notice(char* nickname, char* text)
	{
		return irc_send("NOTICE %s :%s\r\n", nickname, text);
	}

	int IRC::who(char* name, bool operators)
	{
		int result;
		if (operators)
			result = irc_send("WHO %s o\r\n", name);
		else
			result = irc_send("WHO %s\r\n", name);
		return result;
	}

	int IRC::whois(char* nickmasks)
	{
		return irc_send("WHOIS %s\r\n", nickmasks);
	}

	int IRC::whois(char* server, char* nickmasks)
	{
		return irc_send("WHOIS %s %s\r\n", server, nickmasks);
	}

	int IRC::whowas(char* nickname)
	{
		return irc_send("WHOWAS %s\r\n", nickname);
	}

	int IRC::whowas(char* nickname, int count)
	{
		return irc_send("WHOWAS %s %d\r\n", nickname, count);
	}

	int IRC::whowas(char* nickname, int count, char* server)
	{
		return irc_send("WHOWAS %s %d %s\r\n", nickname, count, server);
	}

	int IRC::kill(char* nickname, char* comment)
	{
		return irc_send("KILL %s %s", nickname, comment);
	}

	int IRC::pong(char* daemon)
	{
		return irc_send("PONG %s\r\n", daemon);
	}

	int IRC::pong(char* daemon1, char* daemon2)
	{
		return irc_send("PONG %s %s\r\n", daemon1, daemon2);
	}

	int IRC::away()
	{
		return irc_send("AWAY\r\n");
	}

	int IRC::away(char* message)
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
	//TODO ALL: sscanf :)
	int IRC::summon(char* user)
	{
		return irc_send("SUMMON %s\r\n", user);
	}

	int IRC::summon(char* user, char* server)
	{
		return irc_send("SUMMON %s %s\r\n", user, server);
	}

	int IRC::users()
	{
		return irc_send("USERS\r\n");
	}

	int IRC::users(char* server)
	{
		return irc_send("USERS %s\r\n", server);
	}

	int IRC::wallops(char* text)
	{
		return irc_send("WALLOPS :%s\r\n", text);
	}

	int IRC::userhost(char* nicknames)
	{
		return irc_send("USERHOST %s\r\n", nicknames);
	}

	int IRC::ison(char* nicknames)
	{
		return irc_send("ISON %s\r\n", nicknames);
	}

	int IRC::privmsg(char* receiver, char* text)
	{
		return irc_send("PRIVMSG %s :%s\r\n", receiver, text);
	}

	int IRC::join(char* channels)
	{
		return irc_send("JOIN %s\r\n", channels);
	}

	int IRC::join(char* channels, char* keys)
	{
		return irc_send("JOIN %s %s\r\n", channels, keys);
	}

	int IRC::part(char* channels)
	{
		return irc_send("PART %s\r\n", channels);
	}

	int IRC::kick(char* channel, char* user)
	{
		return irc_send("KICK %s %s\r\n", channel, user);
	}

	int IRC::kick(char* channel, char* user, char* comment)
	{
		return irc_send("KICK %s %s :%s\r\n", channel, user, comment);
	}

	int IRC::raw(char* text)
	{
		return irc_send("%s\r\n", text);
	}

	int IRC::oper(char* user, char * password)
	{
		return irc_send("OPER %s %s\r\n", user, password);
	}

	int IRC::topic(char* channel)
	{
		return irc_send("TOPIC %s\r\n", channel);
	}

	int IRC::topic(char* channel, char* topic)
	{
		return irc_send("TOPIC %s :%s\r\n", channel, topic);
	}

	int IRC::names()
	{
		return irc_send("NAMES\r\n");
	}

	int IRC::names(char* channels)
	{
		return irc_send("NAMES %s\r\n", channels);
	}

	int IRC::mode(char* channel, char* modes, char* limit, char* user, char* banmask)
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

	int IRC::list()
	{
		return irc_send("LIST\r\n");
	}

	int IRC::list(char* channels)
	{
		return irc_send("LIST %s\r\n", channels);
	}

	int IRC::list(char* channels, char* server)
	{
		return irc_send("LIST %s %s\r\n", channels, server);
	}

	int IRC::invite(char* nickname, char* channel)
	{
		return irc_send("INVITE %s %s\r\n", nickname, channel);
	}

	int IRC::mode(char* nickname, char* modes)
	{
		return irc_send("MODE %s %s\r\n", nickname, modes);
	}

	int IRC::nick(char* nickname)
	{
		return irc_send("NICK %s\r\n", nickname);
	}

	int IRC::user(char * username, char * hostname, char * servername, char * realname)
	{
		return irc_send("USER %s %s %s :%s\r\n", username, hostname, servername, realname);
	}
}
