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

//+
IRC::IRC(void (*printFunction)(const char* fmt, ...))
{
	hooks      = 0;
	chan_users = 0;
	connected  = false;
	sentnick   = false;
	sentpass   = false;
	sentuser   = false;
	cur_nick   = 0;
	prnt       = printFunction;
}

//+
IRC::~IRC()
{
	if (connected)
		disconnect();

	clear_irc_command_hook();
}

//+
void IRC::hook_irc_command(char* cmd_name, int (*function_ptr)(char*, irc_reply_data*, void*))
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

//+
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

//+
void IRC::irc_strcpy_s(char* dest, const unsigned int destLen, char* src)
{
#ifdef WIN32
	strcpy_s(dest, destLen, src);
#else
	strcpy(dest, src);
#endif
}

//+
int IRC::irc_send(const char* format, ...)
{
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

//+
int IRC::start(char* server, int port, char* nick, char* user, char* name, char* pass)
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

	if (connect(irc_socket, reinterpret_cast<const sockaddr*>(&rem), sizeof(rem)) == SOCKET_ERROR)
	{
#ifdef WIN32
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
		prnt("Failed to open streams!\n");
		closesocket(irc_socket);
		return IRC_DATASTREAM_OPEN_FAILED;
	}
#endif
	
	connected = true;
	unsigned int nick_length = strlen(nick) + 1;
	cur_nick = new char[nick_length];
	irc_strcpy_s(cur_nick, nick_length, nick);

	if (irc_send("PASS %s\r\n", pass) != IRC_SUCCESS)
		return IRC_SEND_FAILED;

	if (irc_send("NICK %s\r\n", nick) != IRC_SUCCESS)
		return IRC_SEND_FAILED;

	if (irc_send("USER %s * 0 :%s\r\n", user, name) != IRC_SUCCESS)
		return IRC_SEND_FAILED;

	prnt("[cpIRC]: Connected!\n");
	return IRC_SUCCESS;
}

//+
int IRC::disconnect()
{
	if (!connected)
		return IRC_NOT_CONNECTED;

#ifndef WIN32
	if (fclose(dataout))
	{
		return IRC_DATASTREAM_CLOSE_FAILED;
	}
#endif

	if (quit("Leaving") != IRC_SUCCESS)
	{
		return IRC_SEND_FAILED;
	}

#ifdef WIN32
	if(shutdown(irc_socket, 2))
	{
		//prnt("Winsock shutdown() failed: %d\n", WSAGetLastError());
		return IRC_SOCKET_SHUTDOWN_FAILED;
	}
#endif

	if (closesocket(irc_socket))
	{
		return IRC_SOCKET_CLOSE_FAILED;
	}

	connected = false;
	prnt("[cpIRC]: Disconnected from server.\n");
	return IRC_SUCCESS;
}

//+
int IRC::quit(char* quit_message)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	int result;
	if (quit_message)
		result = irc_send("QUIT %s\r\n", quit_message);
	else
		result = irc_send("QUIT\r\n");
	if (result != IRC_SUCCESS)
		return IRC_SEND_FAILED;

	return IRC_SUCCESS;
}

//+
int IRC::message_loop()
{
	char buffer[1024];
	int ret_len;

	if (!connected)
	{
		//prnt("Not connected!\n");
		return IRC_NOT_CONNECTED;
	}

	while (1)
	{
		ret_len = recv(irc_socket, buffer, 1023, 0);
		if (ret_len == SOCKET_ERROR || !ret_len)
		{
			//TODO: wsagetlasterror
			return IRC_RECV_FAILED;
		}

		buffer[ret_len]='\0';
		split_to_replies(buffer);
	}

	return IRC_SUCCESS;
}

//+
void IRC::split_to_replies(char* data)
{
	char* p;

	while (p = strstr(data, "\r\n"))
	{
		*p = '\0';
		parse_irc_reply(data);
		data = p+2;
	}
}
//TODO check all calls
bool IRC::is_op(char* channel, char* nick)
{
	if (!connected)
		return false;

	channel_user* cup = chan_users;

	while (cup)
	{
		if (!strcmp(cup->channel, channel) && !strcmp(cup->nick, nick))
			return cup->flags & IRC_USER_OP ? true : false;
		cup = cup->next;
	}

	return false;
}
//TODO: check all calls
bool IRC::is_voice(char* channel, char* nick)
{
	if (!connected)
		return false;

	channel_user* cup = chan_users;
	
	while (cup)
	{
		if (!strcmp(cup->channel, channel) && !strcmp(cup->nick, nick))
			return cup->flags & IRC_USER_VOICE ? true : false;

		cup = cup->next;
	}

	return false;
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

	hostd_tmp.target=0;

	prnt("%s\n", data);

	if (data[0]==':')
	{
		hostd=&data[1];
		cmd=strchr(hostd, ' ');
		if (!cmd)
			return;
		*cmd='\0';
		cmd++;
		params=strchr(cmd, ' ');
		if (params)
		{
			*params='\0';
			params++;
		}
		hostd_tmp.nick=hostd;
		hostd_tmp.ident=strchr(hostd, '!');
		if (hostd_tmp.ident)
		{
			*hostd_tmp.ident='\0';
			hostd_tmp.ident++;
			hostd_tmp.host=strchr(hostd_tmp.ident, '@');
			if (hostd_tmp.host)
			{
				*hostd_tmp.host='\0';
				hostd_tmp.host++;
			}
		}

		if (!strcmp(cmd, "JOIN"))
		{
			cup=chan_users;
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

			d=0;
			prev=0;
			cup=chan_users;
			while (cup)
			{
				if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
				{
					d=cup;
					break;
				}
				else
				{
					prev=cup;
				}
				cup=cup->next;
			}
			if (d)
			{
				if (d==chan_users)
				{
					chan_users=d->next;
					if (d->channel)
						delete [] d->channel;
					if (d->nick)
						delete [] d->nick;
					delete d;
				}
				else
				{
					if (prev)
					{
						prev->next=d->next;
					}
					chan_users=d->next;
					if (d->channel)
						delete [] d->channel;
					if (d->nick)
						delete [] d->nick;
					delete d;
				}
			}
		}
		else if (!strcmp(cmd, "QUIT"))
		{
			channel_user* d;
			channel_user* prev;

			d=0;
			prev=0;
			cup=chan_users;
			while (cup)
			{
				if (!strcmp(cup->nick, hostd_tmp.nick))
				{
					d=cup;
					if (d==chan_users)
					{
						chan_users=d->next;
						if (d->channel)
							delete [] d->channel;
						if (d->nick)
							delete [] d->nick;
						delete d;
					}
					else
					{
						if (prev)
						{
							prev->next=d->next;
						}
						if (d->channel)
							delete [] d->channel;
						if (d->nick)
							delete [] d->nick;
						delete d;
					}
					break;
				}
				else
				{
					prev=cup;
				}
				cup=cup->next;
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

			chan=params;
			params=strchr(chan, ' ');
			*params='\0';
			params++;
			changevars=params;
			params=strchr(changevars, ' ');
			if (!params)
			{
				return;
			}
			if (chan[0]!='#')
			{
				return;
			}
			*params='\0';
			params++;
		
			plus=false;
			for (i=0; i<(signed)strlen(changevars); i++)
			{
				switch (changevars[i])
				{
				case '+':
					plus=true;
					break;
				case '-':
					plus=false;
					break;
				case 'o':
					tmp=strchr(params, ' ');
					if (tmp)
					{
						*tmp='\0';
						tmp++;
					}
					tmp=params;
					if (plus)
					{
						// user has been opped (chan, params)
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (cup->next && cup->channel)
							{
								if (!strcmp(cup->channel, chan) && !strcmp(cup->nick, tmp))
								{
									d=cup;
									break;
								}
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags|IRC_USER_OP;
						}
					}
					else
					{
						// user has been deopped (chan, params)
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (!strcmp(cup->channel, chan) && !strcmp(cup->nick, tmp))
							{
								d=cup;
								break;
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags^IRC_USER_OP;
						}
					}
					params=tmp;
					break;
				case 'v':
					tmp=strchr(params, ' ');
					if (tmp)
					{
						*tmp='\0';
						tmp++;
					}
					if (plus)
					{
						// user has been voiced
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
							{
								d=cup;
								break;
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags|IRC_USER_VOICE;
						}
					}
					else
					{
						// user has been devoiced
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
							{
								d=cup;
								break;
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags^IRC_USER_VOICE;
						}
					}
					params=tmp;
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
				chan_users=new channel_user;
				chan_users->next=0;
				chan_users->nick=0;
				chan_users->flags=0;
				chan_users->channel=0;
			}
			cup=chan_users;
			chan_temp=strchr(params, '#');
			if (chan_temp)
			{
				//chan_temp+=3;
				p=strstr(chan_temp, " :");
				if (p)
				{
					*p='\0';
					p+=2;
					while (strchr(p, ' '))
					{
						char* tmp;

						tmp=strchr(p, ' ');
						*tmp='\0';
						tmp++;
						while (cup->nick)
						{
							if (!cup->next)
							{
								cup->next=new channel_user;
								cup->next->channel=0;
								cup->next->flags=0;
								cup->next->next=0;
								cup->next->nick=0;
							}
							cup=cup->next;
						}
						if (p[0]=='@')
						{
							cup->flags=cup->flags|IRC_USER_OP;
							p++;
						}
						else if (p[0]=='+')
						{
							cup->flags=cup->flags|IRC_USER_VOICE;
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
							cup->next=new channel_user;
							cup->next->channel=0;
							cup->next->flags=0;
							cup->next->next=0;
							cup->next->nick=0;
						}
						cup=cup->next;
					}
					if (p[0]=='@')
					{
						cup->flags=cup->flags|IRC_USER_OP;
						p++;
					}
					else if (p[0]=='+')
					{
						cup->flags=cup->flags|IRC_USER_VOICE;
						p++;
					}
					unsigned int p_length = strlen(p) + 1;
					cup->nick=new char[p_length];
					irc_strcpy_s(cup->nick, p_length, p);

					unsigned int chan_temp_length = strlen(chan_temp) + 1;
					cup->channel=new char[chan_temp_length];
					irc_strcpy_s(cup->channel, chan_temp_length, chan_temp);
				}
			}
		}
		else if (!strcmp(cmd, "NOTICE"))
		{
			hostd_tmp.target=params;
			params=strchr(hostd_tmp.target, ' ');
			if (params)
				*params='\0';
			params++;
			#ifdef __IRC_DEBUG__
			prnt("%s >-%s- %s\n", hostd_tmp.nick, hostd_tmp.target, &params[1]);
			#endif
		}
		else if (!strcmp(cmd, "PRIVMSG"))
		{
			hostd_tmp.target=params;
			params=strchr(hostd_tmp.target, ' ');
			if (!params)
				return;
			*(params++)='\0';
			#ifdef __IRC_DEBUG__
			prnt("%s: <%s> %s\n", hostd_tmp.target, hostd_tmp.nick, &params[1]);
			#endif
		}
		else if (!strcmp(cmd, "NICK"))
		{
			if (!strcmp(hostd_tmp.nick, cur_nick))
			{
				delete [] cur_nick;
				unsigned int cur_nick_length = strlen(params) + 1;
				cur_nick=new char[cur_nick_length];
				irc_strcpy_s(cur_nick, cur_nick_length, params);
			}
		}
		/* else if (!strcmp(cmd, ""))
		{
			#ifdef __IRC_DEBUG__
			#endif
		} */
		call_hook(cmd, params, &hostd_tmp);
	}
	else
	{
		cmd=data;
		data=strchr(cmd, ' ');
		if (!data)
			return;
		*data='\0';
		params=data+1;

		if (!strcmp(cmd, "PING"))
		{
			if (!params)
				return;
			irc_send("PONG %s\r\n", &params[1]);
			#ifdef __IRC_DEBUG__
			prnt("Ping received, pong sent.\n");
			#endif
		}
		else
		{
			hostd_tmp.host=0;
			hostd_tmp.ident=0;
			hostd_tmp.nick=0;
			hostd_tmp.target=0;
			call_hook(cmd, params, &hostd_tmp);
		}
	}
}
//+
void IRC::call_hook(char* irc_command, char* params, irc_reply_data* hostd)
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
//+ TODO: remove channel
int IRC::notice(char* channel, char* message)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	return irc_send("NOTICE %s :%s\r\n", channel, message);
}
//+ TODO: remove channel
int IRC::notice(char* channel, const char* format, ...)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	va_list argp;
	char buffer[512];

	va_start(argp, format);
	vsnprintf(buffer, 512, format, argp);
	buffer[511] = '\0';
	va_end(argp);

	return notice(channel, buffer);
}

//-
int IRC::privmsg(char* message)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	return irc_send("PRIVMSG #%s :%s\r\n", this->cur_nick, message);
}

//+
int IRC::privmsg(const char* format, ...)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	va_list argp;
	char buffer[512];
	
	va_start(argp, format);
	vsnprintf(buffer, 512, format, argp);
	buffer[511] = '\0';
	va_end(argp);

	return privmsg(buffer);
}

//+
int IRC::join(char* channel)
{
	if (!connected)
		return IRC_NOT_CONNECTED;
	//TODO: save channel
	return irc_send("JOIN %s\r\n", channel);
}
//+
int IRC::part(char* channel)
{
	if (!connected)
		return IRC_NOT_CONNECTED;
	//TODO: remove channel
	return irc_send("PART %s\r\n", channel);
}

//+ TODO: remove channel
int IRC::kick(char* channel, char* nick)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	return irc_send("KICK %s %s\r\n", channel, nick);
}

//+
int IRC::raw(char* data)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	return irc_send("%s\r\n", data);
}

//+ TODO: remove channel
int IRC::kick(char* channel, char* nick, char* message)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	return irc_send("KICK %s %s :%s\r\n", channel, nick, message);
}
//+ TODO: remove channel
//WHAAT?!
int IRC::mode(char* channel, char* modes, char* targets)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	int result;
	if (!targets)
		result = irc_send("MODE %s %s\r\n", channel, modes);
	else
		result = irc_send("MODE %s %s %s\r\n", channel, modes, targets);
	return result;
}
//+ TODO: What?
int IRC::mode(char* modes)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	mode(cur_nick, modes, 0);
	return 0;
}

//+TODO: WHAT?!
int IRC::nick(char* newnick)
{
	if (!connected)
		return IRC_NOT_CONNECTED;

	return irc_send("NICK %s\r\n", newnick);
}

//+
char* IRC::current_nick()
{
	return cur_nick;
}
