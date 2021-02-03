/* This testcase is part of GDB, the GNU debugger.

 Copyright 2021 Free Software Foundation, Inc.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <unistd.h>
#include <x86gprintrin.h>

#define UINTR_HANDLER_REG_SYSCALL 442
#define UINTR_HANDLER_UNREG_SYSCALL 443
#define UINTR_CREATE_FD_SYSCALL 444
#define UINTR_SEND_REG_SYSCALL 445
#define UINTR_SEND_UNREG_SYSCALL 446

unsigned long uintr_received;
unsigned int uintr_fd;

int
uintr_register_handler (void *ui_handler, unsigned int flags)
{
  return syscall (UINTR_HANDLER_REG_SYSCALL, ui_handler, flags);
}

int
uintr_unregister_handler (unsigned int flags)
{
  return syscall (UINTR_HANDLER_UNREG_SYSCALL, flags);
}

int
uintr_create_fd (int vector, unsigned int flags)
{
  return syscall (UINTR_CREATE_FD_SYSCALL, vector, flags);
}

int
uintr_register_sender (int uintr_fd, unsigned int flags)
{
  return syscall (UINTR_SEND_REG_SYSCALL, uintr_fd, flags);
}

int
uintr_unregister_sender (int uintr_fd, unsigned int flags)
{
  return syscall (UINTR_SEND_UNREG_SYSCALL, uintr_fd, flags);
}

void __attribute__ ((interrupt))
ui_handler (struct __uintr_frame *ui_frame, unsigned long long vector)
{
  static const char print[] = "\t-- User Interrupt handler --\n";

  write (STDOUT_FILENO, print, sizeof (print) - 1); /* bp4 */
  uintr_received = 1;
}

void *
sender_thread (void *arg)
{
  int uipi_index;

  uipi_index = uintr_register_sender (uintr_fd, 0);
  if (uipi_index < 0)
    {
      printf ("Sender register error\n");
      exit (EXIT_FAILURE);
    }

  printf ("Sending IPI from sender thread\n");
  _senduipi (uipi_index); /* bp2 */

  uintr_unregister_sender (uintr_fd, 0);

  return NULL;
}

int
main ()
{
  pthread_t pt;
  int ret;

  if (uintr_register_handler (ui_handler, 0))
    {
      printf ("Interrupt handler register error\n");
      exit (EXIT_FAILURE);
    }

  ret = uintr_create_fd (0, 0);
  if (ret < 0)
    {
      printf ("Interrupt vector allocation error\n");
      exit (EXIT_FAILURE);
    }

  uintr_fd = ret;

  _stui ();
  printf ("Receiver enabled interrupts\n");

  if (pthread_create (&pt, NULL, &sender_thread, NULL))
    {
      printf ("Error creating sender thread\n");
      exit (EXIT_FAILURE);
    }

  /* Do some other work */
  while (!uintr_received); /* bp3 */

  pthread_join (pt, NULL); /* bp1 */
  close (uintr_fd);
  uintr_unregister_handler (0);

  printf ("Success\n");
  exit (EXIT_SUCCESS);
}
