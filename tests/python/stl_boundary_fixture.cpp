// SPDX-License-Identifier: Apache-2.0
// External CPython fault provider and public-ledger witness; no product
// headers.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <csignal>

namespace {

constexpr int kObservationMissing = -1;
constexpr Py_ssize_t kSignalHandlerArgumentCount = 2;
constexpr char kModuleName[] = "stl_boundary_fixture";

namespace context_key {
constexpr char kActiveWorkers[] = "active_workers";
constexpr char kActivePayload[] = "active_tracked_payload_bytes";
}  // namespace context_key

namespace snapshot_key {
constexpr char kFormat[] =
    "{s:k,s:i,s:i,s:k,s:k,s:i,s:i,s:i,s:k,s:K,s:K,s:i,s:O,s:i}";
constexpr char kProviderCalls[] = "provider_calls";
constexpr char kProviderErrorActive[] = "provider_error_active";
constexpr char kProviderArmResult[] = "provider_arm_result";
constexpr char kSignalCalls[] = "signal_calls";
constexpr char kSignalThread[] = "signal_thread";
constexpr char kValueWithoutTraceback[] =
    "signal_value_initially_without_traceback";
constexpr char kTracebackDistinct[] = "signal_traceback_distinct";
constexpr char kLegacyTripleAttested[] = "legacy_raw_triple_attested";
constexpr char kWitnessThread[] = "witness_thread";
constexpr char kWitnessWorkers[] = "witness_workers";
constexpr char kWitnessPayload[] = "witness_payload";
constexpr char kWitnessArmResult[] = "witness_arm_result";
constexpr char kArmed[] = "armed";
constexpr char kOwnedReferences[] = "owned_references";
}  // namespace snapshot_key

namespace method_name {
constexpr char kClearReferences[] = "clear_references";
constexpr char kResetWitness[] = "reset_witness";
constexpr char kConfigureSignal[] = "configure_signal";
constexpr char kRawSignalHandler[] = "raw_signal_handler";
constexpr char kProvider[] = "provider";
constexpr char kRetainedVerified[] = "retained_verified";
constexpr char kTryArm[] = "try_arm";
constexpr char kSnapshot[] = "snapshot";
}  // namespace method_name

struct FixtureState {
  PyObject* verified;
  PyObject* signal_type;
  PyObject* signal_value;
  PyObject* signal_traceback;
  unsigned long provider_calls;
  unsigned long signal_calls;
  unsigned long signal_thread;
  unsigned long witness_thread;
  unsigned long long witness_workers;
  unsigned long long witness_payload;
  int provider_error_active;
  int provider_arm_result;
  int signal_value_initially_without_traceback;
  int signal_traceback_distinct;
  int legacy_raw_triple_attested;
  int witness_arm_result;
  bool armed;
};

FixtureState* State(PyObject* module) {
  return static_cast<FixtureState*>(PyModule_GetState(module));
}

int Traverse(PyObject* module, visitproc visit, void* arg) {
  auto* state = State(module);
  if (state == nullptr) return 0;
  Py_VISIT(state->verified);
  Py_VISIT(state->signal_type);
  Py_VISIT(state->signal_value);
  Py_VISIT(state->signal_traceback);
  return 0;
}

int Clear(PyObject* module) {
  auto* state = State(module);
  if (state == nullptr) return 0;
  Py_CLEAR(state->verified);
  Py_CLEAR(state->signal_type);
  Py_CLEAR(state->signal_value);
  Py_CLEAR(state->signal_traceback);
  return 0;
}

void Free(void* module) { (void)Clear(static_cast<PyObject*>(module)); }

PyObject* ClearReferences(PyObject* module, PyObject*) {
  (void)Clear(module);
  Py_RETURN_NONE;
}

PyObject* ResetWitness(PyObject* module, PyObject*) {
  auto* state = State(module);
  state->witness_thread = 0;
  state->witness_workers = 0;
  state->witness_payload = 0;
  state->witness_arm_result = kObservationMissing;
  state->armed = false;
  state->signal_calls = 0;
  state->signal_thread = 0;
  state->legacy_raw_triple_attested = kObservationMissing;
  Py_RETURN_NONE;
}

PyObject* ConfigureSignal(PyObject* module, PyObject* arguments) {
  PyObject* value = nullptr;
  PyObject* traceback = nullptr;
  if (!PyArg_ParseTuple(arguments, "OO", &value, &traceback)) return nullptr;
  if (!PyExceptionInstance_Check(value) || !PyTraceBack_Check(traceback)) {
    PyErr_SetString(PyExc_TypeError,
                    "expected an exception instance and traceback");
    return nullptr;
  }
  // Borrowed inputs become module-owned GC-visible references. A real separate
  // traceback is deliberately supplied to PyErr_Restore by the signal callback.
  PyObject* current =
      PyException_GetTraceback(value);  // New reference or NULL.
  if (current != nullptr) {
    Py_DECREF(current);
    PyErr_SetString(PyExc_ValueError,
                    "signal value must initially have no traceback");
    return nullptr;
  }
  auto* state = State(module);
  Py_XSETREF(state->signal_type,
             Py_NewRef(reinterpret_cast<PyObject*>(Py_TYPE(value))));
  Py_XSETREF(state->signal_value, Py_NewRef(value));
  Py_XSETREF(state->signal_traceback, Py_NewRef(traceback));
  state->signal_value_initially_without_traceback = 1;
  state->signal_traceback_distinct = 1;
  Py_RETURN_NONE;
}

#define MESHVALE_STL_BOUNDARY_RAISED_EXCEPTION_API_VERSION 0x030C0000
PyObject* RawSignalHandler(PyObject* module, PyObject* arguments) {
  auto* state = State(module);
  if (PyTuple_Size(arguments) != kSignalHandlerArgumentCount ||
      state->signal_value == nullptr) {
    PyErr_SetString(PyExc_RuntimeError, "unconfigured signal fixture");
    return nullptr;
  }
  ++state->signal_calls;
  state->signal_thread = PyThread_get_thread_ident();
  PyObject* current = PyException_GetTraceback(state->signal_value);
  state->signal_value_initially_without_traceback = current == nullptr;
  state->signal_traceback_distinct = current != state->signal_traceback;
  Py_XDECREF(current);
  // All three fresh references are stolen by PyErr_Restore. On CPython3.10,
  // value.__traceback__ is still NULL and this separate traceback travels in
  // the raw error triple until the binding's real nanobind fetch normalizes it.
  PyErr_Restore(Py_NewRef(state->signal_type), Py_NewRef(state->signal_value),
                Py_NewRef(state->signal_traceback));
#if PY_VERSION_HEX < MESHVALE_STL_BOUNDARY_RAISED_EXCEPTION_API_VERSION
  // Inspect the genuine active raw triple without normalization. Fetch steals
  // it from the indicator; Restore below returns those exact owned references.
  PyObject* type = nullptr;
  PyObject* value = nullptr;
  PyObject* traceback = nullptr;
  PyErr_Fetch(&type, &value, &traceback);
  current = value == nullptr ? nullptr : PyException_GetTraceback(value);
  state->legacy_raw_triple_attested =
      type == state->signal_type && value == state->signal_value &&
      traceback == state->signal_traceback && current == nullptr;
  Py_XDECREF(current);
  PyErr_Restore(type, value, traceback);
#endif
  return nullptr;
}
#undef MESHVALE_STL_BOUNDARY_RAISED_EXCEPTION_API_VERSION

PyObject* Provider(PyObject* module, PyObject* verified) {
  auto* state = State(module);
  Py_XSETREF(state->verified, Py_NewRef(verified));
  ++state->provider_calls;
  // One actual provider callback: no bytecode or GIL release separates these
  // operations. PyErr_SetInterruptEx does not send an OS process-control
  // signal.
  PyErr_SetString(PyExc_ValueError,
                  "ordinary provider fault with pending SIGINT");
  state->provider_arm_result = PyErr_SetInterruptEx(SIGINT);
  state->provider_error_active = PyErr_ExceptionMatches(PyExc_ValueError);
  return nullptr;
}

PyObject* RetainedVerified(PyObject* module, PyObject*) {
  auto* state = State(module);
  return Py_NewRef(state->verified == nullptr ? Py_None : state->verified);
}

PyObject* TryArm(PyObject* module, PyObject* arguments) {
  PyObject* context = nullptr;
  unsigned long long baseline = 0;
  if (!PyArg_ParseTuple(arguments, "OK", &context, &baseline)) return nullptr;
  auto* state = State(module);
  if (state->armed) Py_RETURN_TRUE;
  // Only public readonly context getters are called. The GIL remains held
  // across both observations and arming, excluding the main Run's next
  // boundary.
  PyObject* value =
      PyObject_GetAttrString(context, context_key::kActiveWorkers);
  if (value == nullptr) return nullptr;
  const auto workers = PyLong_AsUnsignedLongLong(value);
  Py_DECREF(value);
  if (PyErr_Occurred()) return nullptr;
  value = PyObject_GetAttrString(context, context_key::kActivePayload);
  if (value == nullptr) return nullptr;
  const auto payload = PyLong_AsUnsignedLongLong(value);
  Py_DECREF(value);
  if (PyErr_Occurred()) return nullptr;
  if (workers > 0 && payload > baseline) {
    state->witness_thread = PyThread_get_thread_ident();
    state->witness_workers = workers;
    state->witness_payload = payload;
    state->witness_arm_result = PyErr_SetInterruptEx(SIGINT);
    state->armed = state->witness_arm_result == 0;
  }
  return PyBool_FromLong(state->armed);
}

PyObject* Snapshot(PyObject* module, PyObject*) {
  const auto* state = State(module);
  return Py_BuildValue(
      snapshot_key::kFormat, snapshot_key::kProviderCalls,
      state->provider_calls, snapshot_key::kProviderErrorActive,
      state->provider_error_active, snapshot_key::kProviderArmResult,
      state->provider_arm_result, snapshot_key::kSignalCalls,
      state->signal_calls, snapshot_key::kSignalThread, state->signal_thread,
      snapshot_key::kValueWithoutTraceback,
      state->signal_value_initially_without_traceback,
      snapshot_key::kTracebackDistinct, state->signal_traceback_distinct,
      snapshot_key::kLegacyTripleAttested, state->legacy_raw_triple_attested,
      snapshot_key::kWitnessThread, state->witness_thread,
      snapshot_key::kWitnessWorkers, state->witness_workers,
      snapshot_key::kWitnessPayload, state->witness_payload,
      snapshot_key::kWitnessArmResult, state->witness_arm_result,
      snapshot_key::kArmed, state->armed ? Py_True : Py_False,
      snapshot_key::kOwnedReferences,
      (state->verified != nullptr) + (state->signal_type != nullptr) +
          (state->signal_value != nullptr) +
          (state->signal_traceback != nullptr));
}

PyMethodDef kMethods[] = {
    {method_name::kClearReferences, ClearReferences, METH_NOARGS,
     "Release every module-owned test reference."},
    {method_name::kResetWitness, ResetWitness, METH_NOARGS,
     "Reset native work observations."},
    {method_name::kConfigureSignal, ConfigureSignal, METH_VARARGS,
     "Retain a real exception and separately supplied traceback."},
    {method_name::kRawSignalHandler, RawSignalHandler, METH_VARARGS,
     "Signal callable returning a real C-API raised triple."},
    {method_name::kProvider, Provider, METH_O,
     "Provider ordinary-error/pending-SIGINT fault."},
    {method_name::kRetainedVerified, RetainedVerified, METH_NOARGS,
     "Return the actual provider argument."},
    {method_name::kTryArm, TryArm, METH_VARARGS,
     "Arm only at witnessed public native work."},
    {method_name::kSnapshot, Snapshot, METH_NOARGS,
     "Return detached scalar attestations."},
    {nullptr, nullptr, 0, nullptr}};

PyModuleDef kModule = {PyModuleDef_HEAD_INIT,
                       kModuleName,
                       "External CPython boundary qualification fixture.",
                       sizeof(FixtureState),
                       kMethods,
                       nullptr,
                       Traverse,
                       Clear,
                       Free};

}  // namespace

PyMODINIT_FUNC PyInit_stl_boundary_fixture() {
  return PyModule_Create(&kModule);
}
