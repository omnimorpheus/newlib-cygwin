/* select.h

This file is part of Cygwin.

This software is a copyrighted work licensed under the terms of the
Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
details. */

#ifndef _SELECT_H_
#define _SELECT_H_

struct select_record
{
  int fd;
  HANDLE h;
  fhandler_base *fh;
  _cygtls *tls;
  int thread_errno;
  bool windows_handle;
  bool read_ready, write_ready, except_ready;
  bool read_selected, write_selected, except_selected;
  bool except_on_write;
  int (*startup) (select_record *, class select_stuff *);
  int (*peek) (select_record *, bool);
  int (*verify) (select_record *, fd_set *, fd_set *, fd_set *);
  void (*cleanup) (select_record *, class select_stuff *);
  struct select_record *next;
  void set_select_errno () {__seterrno (); thread_errno = errno;}
  int saw_error () {return thread_errno;}
  select_record (int): next (NULL) {}
  select_record () :
    fd (0), h (NULL), fh (NULL), tls (&_my_tls), thread_errno (0),
    windows_handle (false), read_ready (false), write_ready (false),
    except_ready (false), read_selected (false), write_selected (false),
    except_selected (false), except_on_write (false),
    startup (NULL), peek (NULL), verify (NULL), cleanup (NULL),
    next (NULL) {}
#ifdef DEBUGGING
  void dump_select_record ();
#endif
};

struct select_info
{
  cygthread *thread;
  bool stop_thread;
  select_record *start;
  select_info (): thread (NULL), stop_thread (0), start (NULL) {}
};

struct select_pipe_info: public select_info
{
  select_pipe_info (): select_info () {}
};

struct select_socket_info: public select_info
{
  int num_w4;
  LONG *ser_num;
  HANDLE *w4;
  select_socket_info (): select_info (), num_w4 (0), ser_num (0), w4 (NULL) {}
};

struct select_serial_info: public select_info
{
  select_serial_info (): select_info () {}
};

struct select_signalfd_info: public select_info
{
  HANDLE evt;
  select_signalfd_info (): select_info () {}
};

class select_stuff
{
public:
  enum wait_states
  {
    select_signalled = -3,
    select_error = -1,
    select_ok = 0,
    select_set_zero = 1
  };

  ~select_stuff ();
  bool return_on_signal;
  bool always_ready, windows_used;
  select_record start;

  select_pipe_info *device_specific_pipe;
  select_socket_info *device_specific_socket;
  select_serial_info *device_specific_serial;
  select_signalfd_info *device_specific_signalfd;

  bool test_and_set (int, fd_set *, fd_set *, fd_set *);
  int poll (fd_set *, fd_set *, fd_set *);
  wait_states wait (fd_set *, fd_set *, fd_set *, LONGLONG);
  void cleanup ();
  void destroy ();

  select_stuff (): return_on_signal (false), always_ready (false),
		   windows_used (false), start (),
		   device_specific_pipe (NULL),
		   device_specific_socket (NULL),
		   device_specific_serial (NULL),
		   device_specific_signalfd (NULL) {}
};

extern "C" int cygwin_select (int , fd_set *, fd_set *, fd_set *,
			      struct timeval *to);

#endif /* _SELECT_H_ */
