#include <lib/base/eerror.h>
#include <sys/vfs.h> // for statfs
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "myconsole.h"

int bidirpipe(int pfd[], const char *cmd , const char * const argv[], const char *cwd )
{
	int pfddummy[2];  /* HiSilicon dummy */
	int pfdin[2];  /* from child to parent */
	int pfdout[2]; /* from parent to child */
	int pfderr[2]; /* stderr from child to parent */
	int pid;       /* child's pid */

	if ( pipe(pfddummy) == -1 || pipe(pfdin) == -1 || pipe(pfdout) == -1 || pipe(pfderr) == -1 )
		return(-1);
	if ( ( pid = vfork() ) == -1 )
		return(-1);
	else if (pid == 0) /* child process */
	{
		setsid();
		if ( close(0) == -1 || close(1) == -1 || close(2) == -1 )
			_exit(0);

		if (dup(pfdout[0]) != 0 || dup(pfdin[1]) != 1 || dup(pfderr[1]) != 2 )
			_exit(0);

		if (close(pfdout[0]) == -1 || close(pfdout[1]) == -1 || close(pfdin[0]) == -1 || close(pfdin[1]) == -1 || close(pfderr[0]) == -1 || close(pfderr[1]) == -1 )
			_exit(0);

		for (unsigned int i=3; i < 90; ++i )
			close(i);

		if (cwd && chdir(cwd) < 0)
			eDebug("[ServiceApp][eConsoleContainer] failed to change directory to %s (%m)", cwd);

		execvp(cmd, (char * const *)argv);
			/* the vfork will actually suspend the parent thread until execvp is called. thus it's ok to use the shared arg/cmdline pointers here. */
		_exit(0);
	}
	if (close(pfdout[0]) == -1 || close(pfdin[1]) == -1 || close(pfderr[1]) == -1)
			return(-1);

	pfd[0] = pfdin[0];
	pfd[1] = pfdout[1];
	pfd[2] = pfderr[0];

	return(pid);
}

DEFINE_REF(eConsoleContainer);

eConsoleContainer::eConsoleContainer():
	pid(-1),
	killstate(0),
	buffer(2048)
{
	for (int i=0; i < 3; ++i)
	{
		fd[i]=-1;
		filefd[i]=-1;
	}
}

int eConsoleContainer::setCWD( const char *path )
{
	struct stat dir_stat = {};

	if (stat(path, &dir_stat) == -1)
		return -1;

	if (!S_ISDIR(dir_stat.st_mode))
		return -2;

	m_cwd = path;
	return 0;
}

void eConsoleContainer::setBufferSize(int size)
{
	if (size > 0)
		buffer.resize(size);
}


int eConsoleContainer::execute(eMainloop *context, const char *cmd )
{
	int argc = 3;
	const char *argv[argc + 1];
	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = cmd;
	argv[argc] = NULL;

	return execute(context, argv[0], argv);
}

int eConsoleContainer::execute(eMainloop *context, const char *cmdline, const char * const argv[])
{
	if (running())
		return -1;
	eDebug("[ServiceApp][eConsoleContainer] Starting %s", cmdline);
	pid=-1;
	killstate=0;
	int tmp_fd = -1;
	tmp_fd = ::open("/dev/console", O_RDONLY | O_CLOEXEC);
	eDebug("[ServiceApp][eConsoleContainer]  Opened tmp_fd: %d", tmp_fd);
	if (tmp_fd == 0)
	{
		::close(tmp_fd);
		tmp_fd = -1;	
		fd0lock = ::open("/dev/console", O_RDONLY | O_CLOEXEC);
		eDebug("[ServiceApp][eConsoleContainer] opening null fd returned: %d", fd0lock);
	}
	if (tmp_fd != -1)
	{
		::close(tmp_fd);
	}
	/* get one read, one write and the err pipe to the prog..  */
	pid = bidirpipe(fd, cmdline, argv, m_cwd.empty() ? 0 : m_cwd.c_str());

	if ( pid == -1 )
	{
		eDebug("[ServiceApp][eConsoleContainer] failed to start %s", cmdline);
		return -3;
	}

	eDebug("[ServiceApp][eConsoleContainer] pipe in = %d, out = %d, err = %d", fd[0], fd[1], fd[2]);

	::fcntl(fd[0], F_SETFL, O_NONBLOCK);
	::fcntl(fd[1], F_SETFL, O_NONBLOCK);
	::fcntl(fd[2], F_SETFL, O_NONBLOCK);
	in = eSocketNotifier::create(context, fd[0], eSocketNotifier::Read|eSocketNotifier::Priority|eSocketNotifier::Hungup );
	out = eSocketNotifier::create(context, fd[1], eSocketNotifier::Write, false);
	err = eSocketNotifier::create(context, fd[2], eSocketNotifier::Read|eSocketNotifier::Priority );
	CONNECT(in->activated, eConsoleContainer::readyRead);
	CONNECT(out->activated, eConsoleContainer::readyWrite);
	CONNECT(err->activated, eConsoleContainer::readyErrRead);
	in->m_clients.push_back(this);
	out->m_clients.push_back(this);
	err->m_clients.push_back(this);

	return 0;
}

eConsoleContainer::~eConsoleContainer()
{
	kill();
}

void eConsoleContainer::kill()
{
	if ( killstate != -1 && pid != -1 )
	{
		eDebug("[ServiceApp][eConsoleContainer] user kill(SIGKILL) console App");
		killstate=-1;
		/*
		 * Use a negative pid value, to signal the whole process group
		 * ('pid' might not even be running anymore at this point)
		 */
		::kill(-pid, SIGKILL);
		closePipes();
	}
	while( !outbuf.empty() ) // cleanup out buffer
	{
		queue_data d = outbuf.front();
		outbuf.pop();
		delete [] d.data;
	}
	in = 0;
	out = 0;
	err = 0;

	for (int i=0; i < 3; ++i)
	{
		if ( filefd[i] >= 0 )
			close(filefd[i]);
	}
}

void eConsoleContainer::sendCtrlC()
{
	if ( killstate != -1 && pid != -1 )
	{
		eDebug("[ServiceApp][eConsoleContainer] user send SIGINT(Ctrl-C)");
		/*
		 * Use a negative pid value, to signal the whole process group
		 * ('pid' might not even be running anymore at this point)
		 */
		::kill(-pid, SIGINT);
	}
}

void eConsoleContainer::sendEOF()
{
	if (out)
		out->stop();
	if (fd[1] != -1)
	{
		::close(fd[1]);
		fd[1]=-1;
	}
}

void eConsoleContainer::closePipes()
{
	if (in)
		in->stop();
	if (out)
		out->stop();
	if (err)
		err->stop();
	if (fd[0] != -1)
	{
		::close(fd[0]);
		fd[0]=-1;
	}
	if (fd[1] != -1)
	{
		::close(fd[1]);
		fd[1]=-1;
	}
	if (fd[2] != -1)
	{
		::close(fd[2]);
		fd[2]=-1;
	}
	while( outbuf.size() ) // cleanup out buffer
	{
		queue_data d = outbuf.front();
		outbuf.pop();
		delete [] d.data;
	}
	in = 0; out = 0; err = 0;
	pid = -1;
}

void eConsoleContainer::readyRead(int what)
{
	bool hungup = what & eSocketNotifier::Hungup;
	if (what & (eSocketNotifier::Priority|eSocketNotifier::Read))
	{
/*		eDebug("[ServiceApp][eConsoleContainer] readyRead what = %d", what);  */
		char* buf = &buffer[0];
		int rd;
		while((rd = read(fd[0], buf, buffer.size()-1)) > 0)
		{
			buf[rd]=0;
			/*emit*/ dataAvail(buf);
			stdoutAvail(buf);
			if ( filefd[1] >= 0 )
			{
				ssize_t ret = ::write(filefd[1], buf, rd);
				if (ret < 0) eDebug("[ServiceApp][eConsoleContainer]1 write failed: %m");
			}
			if (!hungup)
				break;
		}
	}
	readyErrRead(eSocketNotifier::Priority|eSocketNotifier::Read); /* be sure to flush all data which might be already written */
	if (hungup)
	{
		int childstatus;
		int retval = killstate;
		/*
		 * We have to call 'wait' on the child process, in order to avoid zombies.
		 * Also, this gives us the chance to provide better exit status info to appClosed.
		 */
		if (::waitpid(-pid, &childstatus, 0) > 0)
		{
			if (WIFEXITED(childstatus))
			{
				retval = WEXITSTATUS(childstatus);
			}
		}
		closePipes();
		/*emit*/ appClosed(retval);
	}
}

void eConsoleContainer::readyErrRead(int what)
{
	if (what & (eSocketNotifier::Priority|eSocketNotifier::Read))
	{
/*		eDebug("[ServiceApp][eConsoleContainer] readyErrRead what = %d", what);  */
		char* buf = &buffer[0];
		int rd;
		while((rd = read(fd[2], buf, buffer.size()-1)) > 0)
		{
/*			for ( int i = 0; i < rd; i++ )
				eDebug("[eConsoleContainer] %d = %c (%02x)", i, buf[i], buf[i] );  */
			buf[rd]=0;
			/*emit*/ dataAvail(buf);
			stderrAvail(buf);
		}
	}
}

void eConsoleContainer::write( const char *data, int len )
{
	char *tmp = new char[len];
	memcpy(tmp, data, len);
	outbuf.push(queue_data(tmp,len));
	if (out)
		out->start();
}

void eConsoleContainer::readyWrite(int what)
{
	if (what&eSocketNotifier::Write && outbuf.size() )
	{
		queue_data &d = outbuf.front();
		int wr = ::write( fd[1], d.data+d.dataSent, d.len-d.dataSent );
		if (wr < 0)
		{
/*			eDebug("[ServiceApp][eConsoleContainer]2 write on fd=%d failed: %m", fd[1]);  */
			outbuf.pop();
			delete [] d.data;
			if ( filefd[0] == -1 )
			/* emit */ dataSent(0);
		}			
		else
			d.dataSent += wr;
		if (d.dataSent == d.len)
		{
			outbuf.pop();
			delete [] d.data;
			if ( filefd[0] == -1 )
			/* emit */ dataSent(0);
		}
	}
	if ( !outbuf.size() )
	{
		if ( filefd[0] >= 0 )
		{
			char* buf = &buffer[0];
			int rsize = read(filefd[0], buf, buffer.size());
			if ( rsize > 0 )
				write(buf, rsize);
			else
			{
				close(filefd[0]);
				filefd[0] = -1;
				::close(fd[1]);
/*				eDebug("[ServiceApp][eConsoleContainer] readFromFile done - closing stdin pipe");  */
				fd[1]=-1;
				dataSent(0);
				out->stop();
			}
		}
		else
			out->stop();
	}
}
