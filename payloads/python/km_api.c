/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
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

static PyObject* kontain_snapshot_getdata(PyObject* self, PyObject* args, PyObject* keywds)
{
   int bufsz = 4096;
   PyGILState_STATE state = PyGILState_Ensure();
   void* buf = PyObject_Malloc(bufsz);
   PyGILState_Release(state);
   int ret;

   // 504 = km_snapshot_getdata
   if ((ret = syscall(504, buf, bufsz)) < 0) {
      return PyErr_SetFromErrno(PyExc_RuntimeError);
   }

   PyObject* obj = Py_BuildValue("s#", buf, ret);

   return obj;
}

static PyObject* kontain_snapshot_putdata(PyObject* self, PyObject* args, PyObject* keywds)
{
   char* buf = NULL;
   if (!PyArg_ParseTuple(args, "s", &buf)) {
      // PyArg_ParseTuple set the exception
      return NULL;
   }
   int bufsz = strlen(buf);
   // 503 = km_snapshot_putdata
   int ret;
   if ((ret = syscall(503, buf, bufsz)) < 0) {
      return PyErr_SetFromErrno(PyExc_RuntimeError);
   }

   return Py_None;
}

static PyMethodDef KontainMethods[] = {
    {.ml_name = "take",
     .ml_meth = kontain_snapshot_take,
     .ml_flags = METH_VARARGS | METH_KEYWORDS,
     .ml_doc = "Take process snapshot"},
    {.ml_name = "getdata",
     .ml_meth = kontain_snapshot_getdata,
     .ml_flags = METH_VARARGS | METH_KEYWORDS,
     .ml_doc = "Read snapshot start data from KM"},
    {.ml_name = "putdata",
     .ml_meth = kontain_snapshot_putdata,
     .ml_flags = METH_VARARGS | METH_KEYWORDS,
     .ml_doc = "Write snapshot completion data"},
    {} /* Sentinel */
};

static struct PyModuleDef kontain_snapshot_module = {.m_base = PyModuleDef_HEAD_INIT,
                                                     .m_name = "snapshot",
                                                     .m_doc = NULL,
                                                     .m_size = -1,
                                                     .m_methods = KontainMethods};

static struct PyModuleDef kontain_module = {.m_base = PyModuleDef_HEAD_INIT,
                                            .m_name = "kontain",
                                            .m_doc = NULL,
                                            .m_size = -1,
                                            .m_methods = NULL};

PyMODINIT_FUNC PyInit_kontain(void)
{
   PyObject* kontain_m = PyModule_Create(&kontain_module);
   if (kontain_m == NULL) {
      return NULL;
   }
   PyObject* kontain_snapshot_m = PyModule_Create(&kontain_snapshot_module);
   if (kontain_snapshot_m == NULL) {
      Py_DECREF(kontain_m);
      return NULL;
   }
   if (PyModule_AddObject(kontain_m, "snapshots", kontain_snapshot_m) < 0) {
      Py_DECREF(kontain_snapshot_m);
      Py_DECREF(kontain_m);
      return NULL;
   }
   return kontain_m;
}
