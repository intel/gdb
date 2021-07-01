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

#include "../lib/sycl-util.cpp"
#include <CL/sycl.hpp>

struct inner_struct
{
    int m_public;
    char m_public_c;
    void setPublic (int n) { m_public = n; }
  protected:
    int m_protected;
    char m_protected_c;
    void setProtected (int n) { m_protected = n; }
  private:
    int m_private;
    char m_private_c;
    void setPrivate (int n) { m_private = n; }
};

class inner_class
{
    int m_private;
    void setPrivate (int n) { m_private = n; }
  protected:
    int m_protected;
    void setProtected (int n) { m_protected = n; }
  public:
    int m_public;
    void setPublic (int n) { m_public = n; }
};

struct outer_struct
{
    int m_public;
    inner_struct m_public_s;
    inner_class m_public_c;
    void setPublic (int n) { m_public = n; }
  protected:
    int m_protected;
    inner_struct m_protected_s;
    inner_class m_protected_c;
    void setProtected (int n) { m_protected = n; }
  private:
    int m_private;
    inner_struct m_private_s;
    inner_class m_private_c;
    void setPrivate (int n) { m_private = n; }
};

class outer_class
{
    int m_private;
    inner_struct m_private_s;
    inner_class m_private_c;
    void setPrivate (int n) { m_private = n; }
  protected:
    int m_protected;
    inner_struct m_protected_s;
    inner_class m_protected_c;
    void setProtected (int n) { m_protected = n; }
  public:
    int m_public;
    inner_struct m_public_s;
    inner_class m_public_c;
    void setPublic (int n) { m_public = n; }
};

int
main (int argc, char *argv[])
{
  outer_struct sObj;
  outer_class cObj;

  {
    cl::sycl::queue queue = { get_sycl_queue (argc, argv) };
    cl::sycl::buffer<outer_struct, 1> buffer = { &sObj, cl::sycl::range<1> { 1 } };
    queue.submit ([&] (cl::sycl::handler &cgh)
      {
	auto input = buffer.get_access<cl::sycl::access::mode::read_write> (cgh);
	cgh.single_task<class simple_kernel> ([=] ()
	  {
	    outer_struct sObj;
	    outer_class cObj;

	    sObj.setPublic (1);
	    cObj.setPublic (1);
	    /* Dummy code, makes sure the kernel is not optimized out.  */
	    input[0].setPublic(1); /* kernel-line */
	  });
      });
  }

  sObj.setPublic (1);  /* host-line */
  cObj.setPublic (1);

  return 0;
}
