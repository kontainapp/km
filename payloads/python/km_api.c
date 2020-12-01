/*
 * Copyright © 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Kontain API bindings for Python
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#define __GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

static PyObject* kontain_snapshot_take(PyObject* self, PyObject* args, PyObject* keywds)
{
   char* label = NULL;
   char* description = NULL;
   int live = 0;
   static char* kwlist[] = {"label", "description", "live"};

   if (!PyArg_ParseTupleAndKeywords(args, keywds, "|ssi", kwlist, &label, &description, &live)) {
      return NULL;
   }

   // KM Hypercall 505 == snapshot
   syscall(505, label, description, live);
   return Py_None;
}

static PyMethodDef KontainMethods[] = {
    {.ml_name = "snapshot_take",
     .ml_meth = kontain_snapshot_take,
     .ml_flags = METH_VARARGS | METH_KEYWORDS,
     .ml_doc = "Take process snapshot"},
    {} /* Sentinel */
};

static struct PyModuleDef kontain_module = {.m_base = PyModuleDef_HEAD_INIT,
                                            .m_name = "kontain",
                                            .m_doc = NULL,
                                            .m_size = -1,
                                            .m_methods = KontainMethods};

PyMODINIT_FUNC PyInit_kontain(void)
{
   return PyModule_Create(&kontain_module);
}