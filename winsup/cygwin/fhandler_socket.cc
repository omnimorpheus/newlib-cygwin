/* fhandler_socket.cc. See fhandler.h for a description of the fhandler classes.

   This file is part of Cygwin.

   This software is a copyrighted work licensed under the terms of the
   Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
   details. */

#define  __INSIDE_CYGWIN_NET__
#define USE_SYS_TYPES_FD_SET

#include "winsup.h"
#ifdef __x86_64__
/* 2014-04-24: Current Mingw headers define sockaddr_in6 using u_long (8 byte)
   because a redefinition for LP64 systems is missing.  This leads to a wrong
   definition and size of sockaddr_in6 when building with winsock headers.
   This definition is also required to use the right u_long type in subsequent
   function calls. */
#undef u_long
#define u_long __ms_u_long
#endif
#include <ntsecapi.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <iphlpapi.h>
#include "cygerrno.h"
#include "security.h"
#include "path.h"
#include "fhandler.h"
#include "dtable.h"
#include "cygheap.h"
#include <asm/byteorder.h>
#include "cygwin/version.h"
#include "perprocess.h"
#include "shared_info.h"
#include "sigproc.h"
#include "wininfo.h"
#include <unistd.h>
#include <sys/param.h>
#include <sys/statvfs.h>
#include <cygwin/acl.h>
#include "cygtls.h"
#include <sys/un.h>
#include "ntdll.h"
#include "miscfuncs.h"
#include "tls_pbuf.h"

#define ASYNC_MASK (FD_READ|FD_WRITE|FD_OOB|FD_ACCEPT|FD_CONNECT)
#define EVENT_MASK (FD_READ|FD_WRITE|FD_OOB|FD_ACCEPT|FD_CONNECT|FD_CLOSE)

extern "C" {
  int sscanf (const char *, const char *, ...);
} /* End of "C" section */

/**********************************************************************/
/* fhandler_socket */

fhandler_socket::fhandler_socket () :
  fhandler_base (),
  uid (myself->uid),
  gid (myself->gid),
  mode (S_IFSOCK | S_IRWXU | S_IRWXG | S_IRWXO),
  _rcvtimeo (INFINITE),
  _sndtimeo (INFINITE),
  status ()
{
}

fhandler_socket::~fhandler_socket ()
{
}

char *
fhandler_socket::get_proc_fd_name (char *buf)
{
  __small_sprintf (buf, "socket:[%lu]", get_plain_ino ());
  return buf;
}

int
fhandler_socket::getpeereid (pid_t *pid, uid_t *euid, gid_t *egid)
{
  set_errno (EINVAL);
  return -1;
}

/* Definitions of old ifreq stuff used prior to Cygwin 1.7.0. */
#define OLD_SIOCGIFFLAGS    _IOW('s', 101, struct __old_ifreq)
#define OLD_SIOCGIFADDR     _IOW('s', 102, struct __old_ifreq)
#define OLD_SIOCGIFBRDADDR  _IOW('s', 103, struct __old_ifreq)
#define OLD_SIOCGIFNETMASK  _IOW('s', 104, struct __old_ifreq)
#define OLD_SIOCGIFHWADDR   _IOW('s', 105, struct __old_ifreq)
#define OLD_SIOCGIFMETRIC   _IOW('s', 106, struct __old_ifreq)
#define OLD_SIOCGIFMTU      _IOW('s', 107, struct __old_ifreq)
#define OLD_SIOCGIFINDEX    _IOW('s', 108, struct __old_ifreq)

#define CONV_OLD_TO_NEW_SIO(old) (((old)&0xff00ffff)|(((long)sizeof(struct ifreq)&IOCPARM_MASK)<<16))

struct __old_ifreq {
#define __OLD_IFNAMSIZ	16
  union {
    char    ifrn_name[__OLD_IFNAMSIZ];   /* if name, e.g. "en0" */
  } ifr_ifrn;

  union {
    struct  sockaddr ifru_addr;
    struct  sockaddr ifru_broadaddr;
    struct  sockaddr ifru_netmask;
    struct  sockaddr ifru_hwaddr;
    short   ifru_flags;
    int     ifru_metric;
    int     ifru_mtu;
    int     ifru_ifindex;
  } ifr_ifru;
};

int
fhandler_socket::ioctl (unsigned int cmd, void *p)
{
  extern int get_ifconf (struct ifconf *ifc, int what); /* net.cc */
  int res;
  struct ifconf ifc, *ifcp;
  struct ifreq *ifrp;

  switch (cmd)
    {
    case SIOCGIFCONF:
      ifcp = (struct ifconf *) p;
      if (!ifcp)
	{
	  set_errno (EINVAL);
	  return -1;
	}
      if (CYGWIN_VERSION_CHECK_FOR_OLD_IFREQ)
	{
	  ifc.ifc_len = ifcp->ifc_len / sizeof (struct __old_ifreq)
			* sizeof (struct ifreq);
	  ifc.ifc_buf = (caddr_t) alloca (ifc.ifc_len);
	}
      else
	{
	  ifc.ifc_len = ifcp->ifc_len;
	  ifc.ifc_buf = ifcp->ifc_buf;
	}
      res = get_ifconf (&ifc, cmd);
      if (res)
	debug_printf ("error in get_ifconf");
      if (CYGWIN_VERSION_CHECK_FOR_OLD_IFREQ)
	{
	  struct __old_ifreq *ifr = (struct __old_ifreq *) ifcp->ifc_buf;
	  for (ifrp = ifc.ifc_req;
	       (caddr_t) ifrp < ifc.ifc_buf + ifc.ifc_len;
	       ++ifrp, ++ifr)
	    {
	      memcpy (&ifr->ifr_ifrn, &ifrp->ifr_ifrn, sizeof ifr->ifr_ifrn);
	      ifr->ifr_name[__OLD_IFNAMSIZ - 1] = '\0';
	      memcpy (&ifr->ifr_ifru, &ifrp->ifr_ifru, sizeof ifr->ifr_ifru);
	    }
	  ifcp->ifc_len = ifc.ifc_len / sizeof (struct ifreq)
			  * sizeof (struct __old_ifreq);
	}
      else
	ifcp->ifc_len = ifc.ifc_len;
      break;
    case OLD_SIOCGIFFLAGS:
    case OLD_SIOCGIFADDR:
    case OLD_SIOCGIFBRDADDR:
    case OLD_SIOCGIFNETMASK:
    case OLD_SIOCGIFHWADDR:
    case OLD_SIOCGIFMETRIC:
    case OLD_SIOCGIFMTU:
    case OLD_SIOCGIFINDEX:
      cmd = CONV_OLD_TO_NEW_SIO (cmd);
      /*FALLTHRU*/
    case SIOCGIFFLAGS:
    case SIOCGIFBRDADDR:
    case SIOCGIFNETMASK:
    case SIOCGIFADDR:
    case SIOCGIFHWADDR:
    case SIOCGIFMETRIC:
    case SIOCGIFMTU:
    case SIOCGIFINDEX:
    case SIOCGIFFRNDLYNAM:
    case SIOCGIFDSTADDR:
      {
	if (!p)
	  {
	    debug_printf ("ifr == NULL");
	    set_errno (EINVAL);
	    return -1;
	  }

	if (cmd > SIOCGIFINDEX && CYGWIN_VERSION_CHECK_FOR_OLD_IFREQ)
	  {
	    debug_printf ("cmd not supported on this platform");
	    set_errno (EINVAL);
	    return -1;
	  }
	ifc.ifc_len = 64 * sizeof (struct ifreq);
	ifc.ifc_buf = (caddr_t) alloca (ifc.ifc_len);
	if (cmd == SIOCGIFFRNDLYNAM)
	  {
	    struct ifreq_frndlyname *iff = (struct ifreq_frndlyname *)
				alloca (64 * sizeof (struct ifreq_frndlyname));
	    for (int i = 0; i < 64; ++i)
	      ifc.ifc_req[i].ifr_frndlyname = &iff[i];
	  }

	res = get_ifconf (&ifc, cmd);
	if (res)
	  {
	    debug_printf ("error in get_ifconf");
	    break;
	  }

	if (CYGWIN_VERSION_CHECK_FOR_OLD_IFREQ)
	  {
	    struct __old_ifreq *ifr = (struct __old_ifreq *) p;
	    debug_printf ("    name: %s", ifr->ifr_name);
	    for (ifrp = ifc.ifc_req;
		 (caddr_t) ifrp < ifc.ifc_buf + ifc.ifc_len;
		 ++ifrp)
	      {
		debug_printf ("testname: %s", ifrp->ifr_name);
		if (! strcmp (ifrp->ifr_name, ifr->ifr_name))
		  {
		    memcpy (&ifr->ifr_ifru, &ifrp->ifr_ifru,
			    sizeof ifr->ifr_ifru);
		    break;
		  }
	      }
	  }
	else
	  {
	    struct ifreq *ifr = (struct ifreq *) p;
	    debug_printf ("    name: %s", ifr->ifr_name);
	    for (ifrp = ifc.ifc_req;
		 (caddr_t) ifrp < ifc.ifc_buf + ifc.ifc_len;
		 ++ifrp)
	      {
		debug_printf ("testname: %s", ifrp->ifr_name);
		if (! strcmp (ifrp->ifr_name, ifr->ifr_name))
		  {
		    if (cmd == SIOCGIFFRNDLYNAM)
		      /* The application has to care for the space. */
		      memcpy (ifr->ifr_frndlyname, ifrp->ifr_frndlyname,
			      sizeof (struct ifreq_frndlyname));
		    else
		      memcpy (&ifr->ifr_ifru, &ifrp->ifr_ifru,
			      sizeof ifr->ifr_ifru);
		    break;
		  }
	      }
	  }
	if ((caddr_t) ifrp >= ifc.ifc_buf + ifc.ifc_len)
	  {
	    set_errno (EINVAL);
	    return -1;
	  }
	break;
      }
    default:
      res = fhandler_base::ioctl (cmd, p);
      break;
    }
  syscall_printf ("%d = ioctl_socket(%x, %p)", res, cmd, p);
  return res;
}

int
fhandler_socket::fcntl (int cmd, intptr_t arg)
{
  int res = 0;
  int request, current;

  switch (cmd)
    {
    case F_SETFL:
      {
	/* Carefully test for the O_NONBLOCK or deprecated OLD_O_NDELAY flag.
	   Set only the flag that has been passed in.  If both are set, just
	   record O_NONBLOCK.   */
	int new_flags = arg & O_NONBLOCK_MASK;
	if ((new_flags & OLD_O_NDELAY) && (new_flags & O_NONBLOCK))
	  new_flags = O_NONBLOCK;
	current = get_flags () & O_NONBLOCK_MASK;
	request = new_flags ? 1 : 0;
	if (!!current != !!new_flags && (res = ioctl (FIONBIO, &request)))
	  break;
	set_flags ((get_flags () & ~O_NONBLOCK_MASK) | new_flags);
	break;
      }
    default:
      res = fhandler_base::fcntl (cmd, arg);
      break;
    }
  return res;
}

int
fhandler_socket::open (int flags, mode_t mode)
{
  set_errno (ENXIO);
  return 0;
}

int __reg2
fhandler_socket::fstat (struct stat *buf)
{
  int res;

  res = fhandler_socket::fstat (buf);
  if (!res)
    {
      buf->st_dev = FHDEV (DEV_SOCK_MAJOR, 0);
      if (!(buf->st_ino = get_plain_ino ()))
	sscanf (get_name (), "/proc/%*d/fd/socket:[%lld]",
			     (long long *) &buf->st_ino);
      buf->st_uid = uid;
      buf->st_gid = gid;
      buf->st_mode = mode;
      buf->st_size = 0;
    }
  return res;
}

int __reg2
fhandler_socket::fstatvfs (struct statvfs *sfs)
{
  memset (sfs, 0, sizeof (*sfs));
  sfs->f_bsize = sfs->f_frsize = 4096;
  sfs->f_namemax = NAME_MAX;
  return 0;
}

int
fhandler_socket::fchmod (mode_t newmode)
{
  mode = (newmode & ~S_IFMT) | S_IFSOCK;
  return 0;
}

int
fhandler_socket::fchown (uid_t newuid, gid_t newgid)
{
  bool perms = check_token_membership (&well_known_admins_sid);

  /* Admin rulez */
  if (!perms)
    {
      /* Otherwise, new uid == old uid or current uid is fine */
      if (newuid == ILLEGAL_UID || newuid == uid || newuid == myself->uid)
	perms = true;
      /* Otherwise, new gid == old gid or current gid is fine */
      else if (newgid == ILLEGAL_GID || newgid == gid || newgid == myself->gid)
	perms = true;
      else
	{
	  /* Last but not least, newgid in supplementary group list is fine */
	  tmp_pathbuf tp;
	  gid_t *gids = (gid_t *) tp.w_get ();
	  int num = getgroups (65536 / sizeof (*gids), gids);

	  for (int idx = 0; idx < num; ++idx)
	    if (newgid == gids[idx])
	      {
		perms = true;
		break;
	      }
	}
   }

  if (perms)
    {
      if (newuid != ILLEGAL_UID)
	uid = newuid;
      if (newgid != ILLEGAL_GID)
	gid = newgid;
      return 0;
    }
  set_errno (EPERM);
  return -1;
}

int
fhandler_socket::facl (int cmd, int nentries, aclent_t *aclbufp)
{
  set_errno (EOPNOTSUPP);
  return -1;
}

int
fhandler_socket::link (const char *newpath)
{
  return fhandler_base::link (newpath);
}
