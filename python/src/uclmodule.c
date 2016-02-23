// Attempts to load a UCL structure from a string
#include <ucl.h>
#include <Python.h>

static PyObject*
_basic_ucl_type(ucl_object_t const * const obj) {
	if (obj->type == UCL_INT) {
		return Py_BuildValue("L", (long long)ucl_object_toint (obj));
	}
	else if (obj->type == UCL_FLOAT) {
		return Py_BuildValue("d", ucl_object_todouble (obj));
	}
	else if (obj->type == UCL_STRING) {
		return Py_BuildValue("s", ucl_object_tostring (obj));
	}
	else if (obj->type == UCL_BOOLEAN) {
		// maybe used 'p' here?
		return Py_BuildValue("s", ucl_object_tostring_forced (obj));
	}
	else if (obj->type == UCL_TIME) {
		return Py_BuildValue("d", ucl_object_todouble (obj));
	}
	return NULL;
}

static PyObject*
_iterate_valid_ucl(ucl_object_t const * obj) {
	const ucl_object_t *tmp;
	ucl_object_iter_t it = NULL;

	tmp = obj;

	while ((obj = ucl_object_iterate (tmp, &it, false))) {

		PyObject* val;

		val = _basic_ucl_type(obj);
		if (!val) {
			PyObject* key = NULL;
			if (obj->key != NULL) {
				key = Py_BuildValue("s", ucl_object_key(obj));
			}

			PyObject* ret;
			ret = PyDict_New();
			if (obj->type == UCL_OBJECT) {
				val = PyDict_New();
				const ucl_object_t *cur;
				ucl_object_iter_t it_obj = NULL;
				while ((cur = ucl_object_iterate (obj, &it_obj, true))) {
					PyObject* keyobj = Py_BuildValue("s",ucl_object_key(cur));
					PyDict_SetItem(val, keyobj, _iterate_valid_ucl(cur));
				}
			}
			else if (obj->type == UCL_ARRAY) {
				val = PyList_New(0);
				const ucl_object_t *cur;
				ucl_object_iter_t it_obj = NULL;
				while ((cur = ucl_object_iterate (obj, &it_obj, true))) {
					PyList_Append(val, _iterate_valid_ucl(cur));
				}
			}
			else if (obj->type == UCL_USERDATA) {
				// XXX: this should be
				// PyBytes_FromStringAndSize; where is the
				// length from?
				val = PyBytes_FromString(obj->value.ud);
			}
		}
		return val;
	}

	PyErr_SetString(PyExc_SystemError, "unhandled type");
	return NULL;
}

static PyObject*
_internal_load_ucl(char* uclstr) {
	PyObject* ret;

	struct ucl_parser *parser = ucl_parser_new (UCL_PARSER_NO_TIME);

	bool r = ucl_parser_add_string(parser, uclstr, 0);
	if (r) {
		if (ucl_parser_get_error (parser)) {
			PyErr_SetString(PyExc_ValueError, ucl_parser_get_error(parser));
			ucl_parser_free(parser);
			ret = NULL;
			goto return_with_parser;
		} else {
			ucl_object_t* uclobj = ucl_parser_get_object(parser);
			ret = _iterate_valid_ucl(uclobj);
			ucl_object_unref(uclobj);
			goto return_with_parser;
		}

	} else {
		PyErr_SetString(PyExc_ValueError, ucl_parser_get_error (parser));
		ret = NULL;
		goto return_with_parser;
	}

return_with_parser:
	ucl_parser_free(parser);
	return ret;
}

static PyObject*
ucl_load(PyObject *self, PyObject *args) {
	char* uclstr;
	if (PyArg_ParseTuple(args, "z", &uclstr)) {
		if (!uclstr) {
			Py_RETURN_NONE;
		}
		return _internal_load_ucl(uclstr);
	}
	return NULL;
}

static ucl_object_t*
_internal_parse_ucl(char *uclstr, const char *filename)
{
	struct ucl_parser* parser = ucl_parser_new (0);
	ucl_parser_set_filename(parser, filename);

	ucl_parser_add_string (parser, uclstr, 0);

	if (ucl_parser_get_error (parser) != NULL) {
		PyErr_SetString(PyExc_ValueError, ucl_parser_get_error(parser));
		return NULL;
	}
	ucl_object_t* obj = ucl_parser_get_object (parser);
	ucl_parser_free (parser);

	return obj;
}

static PyObject*
ucl_validate(PyObject *self, PyObject *args) {
	char  *uclstr, *schemastr;
	if (PyArg_ParseTuple(args, "zz", &schemastr, &uclstr)) {
		if (!schemastr || !uclstr) {
			Py_RETURN_NONE;
		}

		ucl_object_t* ucl = _internal_parse_ucl(uclstr, "configuration text");
		if (!ucl) return NULL;

		ucl_object_t* schema = _internal_parse_ucl(schemastr, "schema text");
		if (!schema) {
			ucl_object_unref(ucl);
			return NULL;
		}

		struct ucl_schema_error err;

		if (!ucl_object_validate(schema, ucl, &err)) {
			char errorstr[1024];
			snprintf(errorstr, sizeof(errorstr), "%s: %s", err.obj->key, err.msg);
			errorstr[sizeof(errorstr)-1] = '\0';

			PyErr_SetString(PyExc_ValueError, errorstr);
			return NULL;
		};

		Py_RETURN_TRUE;
	}
	return NULL;
}

static PyMethodDef uclMethods[] = {
	{"load", ucl_load, METH_VARARGS, "Load UCL from stream"},
	{"validate", ucl_validate, METH_VARARGS, "Validate ucl stream against schema"},
	{NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef uclmodule = {
	PyModuleDef_HEAD_INIT,
	"ucl",
	NULL,
	-1,
	uclMethods
};

PyMODINIT_FUNC
PyInit_ucl(void) {
	return PyModule_Create(&uclmodule);
}
#else
void initucl(void) {
	Py_InitModule("ucl", uclMethods);
}
#endif
