#!/usr/bin/env python3

import argparse
import ctypes
import re
from dataclasses import dataclass
from pathlib import Path


ECMC_NATIVE_TYPE_BOOL = 1
ECMC_NATIVE_TYPE_S8 = 2
ECMC_NATIVE_TYPE_U8 = 3
ECMC_NATIVE_TYPE_S16 = 4
ECMC_NATIVE_TYPE_U16 = 5
ECMC_NATIVE_TYPE_S32 = 6
ECMC_NATIVE_TYPE_U32 = 7
ECMC_NATIVE_TYPE_F32 = 8
ECMC_NATIVE_TYPE_F64 = 9
ECMC_NATIVE_TYPE_U64 = 10
ECMC_NATIVE_TYPE_S64 = 11


class ExportedVar(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("data", ctypes.c_void_p),
        ("type", ctypes.c_uint32),
        ("bytes", ctypes.c_uint32),
        ("writable", ctypes.c_uint32),
    ]


class CppLogicApi(ctypes.Structure):
    pass


SET_HOST = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
CREATE_INSTANCE = ctypes.CFUNCTYPE(ctypes.c_void_p)
DESTROY_INSTANCE = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
RUN_CYCLE = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
GET_EXPORTED_VARS = ctypes.CFUNCTYPE(ctypes.POINTER(ExportedVar), ctypes.c_void_p)
GET_EXPORTED_VAR_COUNT = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p)


CppLogicApi._fields_ = [
    ("abiVersion", ctypes.c_uint32),
    ("name", ctypes.c_char_p),
    ("setHostServices", SET_HOST),
    ("createInstance", CREATE_INSTANCE),
    ("destroyInstance", DESTROY_INSTANCE),
    ("runCycle", RUN_CYCLE),
    ("getItemBindings", ctypes.c_void_p),
    ("getItemBindingCount", ctypes.c_void_p),
    ("getExportedVars", GET_EXPORTED_VARS),
    ("getExportedVarCount", GET_EXPORTED_VAR_COUNT),
]


@dataclass
class TemplateSpec:
    template: str
    columns: tuple[str, ...]
    values: dict[str, str]


def sanitize_record_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-") or "unnamed"


def type_to_ftvl(value_type: int) -> str:
    if value_type == ECMC_NATIVE_TYPE_BOOL:
        return "UCHAR"
    if value_type == ECMC_NATIVE_TYPE_S8:
        return "CHAR"
    if value_type == ECMC_NATIVE_TYPE_U8:
        return "UCHAR"
    if value_type == ECMC_NATIVE_TYPE_S16:
        return "SHORT"
    if value_type == ECMC_NATIVE_TYPE_U16:
        return "USHORT"
    if value_type in (ECMC_NATIVE_TYPE_S32, ECMC_NATIVE_TYPE_U32):
        return "LONG"
    if value_type == ECMC_NATIVE_TYPE_F32:
        return "FLOAT"
    return "DOUBLE"


def scalar_template(value_type: int, writable: bool) -> str:
    if value_type == ECMC_NATIVE_TYPE_BOOL:
        return "ecmcCppLogicBo.template" if writable else "ecmcCppLogicBi.template"
    if value_type in (
        ECMC_NATIVE_TYPE_S8,
        ECMC_NATIVE_TYPE_U8,
        ECMC_NATIVE_TYPE_S16,
        ECMC_NATIVE_TYPE_U16,
        ECMC_NATIVE_TYPE_S32,
        ECMC_NATIVE_TYPE_U32,
    ):
        return "ecmcCppLogicLongOut.template" if writable else "ecmcCppLogicLongIn.template"
    return "ecmcCppLogicAo.template" if writable else "ecmcCppLogicAi.template"


def build_template_spec(param_prefix: str, export_var: ExportedVar) -> TemplateSpec:
    name = export_var.name.decode()
    param_name = f"{param_prefix}{name}" if param_prefix else name
    rec_name = sanitize_record_name(name)
    is_array = export_var.bytes > type_size(export_var.type)

    if is_array:
        template = (
            "ecmcCppLogicWaveformOut.template"
            if export_var.writable
            else "ecmcCppLogicWaveformIn.template"
        )
        values = {
            "REC": rec_name,
            "PARAM": param_name,
            "DESC": name,
            "NELM": str(export_var.bytes // max(type_size(export_var.type), 1)),
            "FTVL": type_to_ftvl(export_var.type),
        }
        return TemplateSpec(template, ("REC", "PARAM", "DESC", "NELM", "FTVL"), values)

    template = scalar_template(export_var.type, bool(export_var.writable))
    values = {
        "REC": rec_name,
        "PARAM": param_name,
        "DESC": name,
    }
    columns = ("REC", "PARAM", "DESC")
    if template in ("ecmcCppLogicAi.template", "ecmcCppLogicAo.template"):
        values["PREC"] = "3"
        columns = ("REC", "PARAM", "DESC", "PREC")
    return TemplateSpec(template, columns, values)


def type_size(value_type: int) -> int:
    return {
        ECMC_NATIVE_TYPE_BOOL: 1,
        ECMC_NATIVE_TYPE_S8: 1,
        ECMC_NATIVE_TYPE_U8: 1,
        ECMC_NATIVE_TYPE_S16: 2,
        ECMC_NATIVE_TYPE_U16: 2,
        ECMC_NATIVE_TYPE_S32: 4,
        ECMC_NATIVE_TYPE_U32: 4,
        ECMC_NATIVE_TYPE_F32: 4,
        ECMC_NATIVE_TYPE_F64: 8,
        ECMC_NATIVE_TYPE_U64: 8,
        ECMC_NATIVE_TYPE_S64: 8,
    }.get(value_type, 1)


def load_exports(logic_lib: Path) -> tuple[str, list[ExportedVar]]:
    lib = ctypes.CDLL(str(logic_lib))
    get_api = None
    for symbol_name in ("ecmc_cpp_logic_get_api", "ecmc_native_logic_get_api"):
        if hasattr(lib, symbol_name):
            get_api = getattr(lib, symbol_name)
            break
    if get_api is None:
        raise AttributeError("Missing ecmc_cpp_logic_get_api/ecmc_native_logic_get_api")
    get_api.restype = ctypes.POINTER(CppLogicApi)
    api = get_api().contents
    logic_name = api.name.decode() if api.name else logic_lib.stem

    instance = api.createInstance()
    try:
        count = api.getExportedVarCount(instance)
        exports = api.getExportedVars(instance)
        return logic_name, [exports[i] for i in range(count)]
    finally:
        api.destroyInstance(instance)


def render_substitutions(logic_name: str, specs: list[TemplateSpec]) -> str:
    lines = [
        "# Auto-generated C++ logic substitutions",
        f"# Logic: {logic_name}",
        'global { P = "$(P)" PORT = "$(PORT)" }',
        "",
    ]

    grouped: dict[str, list[TemplateSpec]] = {}
    for spec in specs:
        grouped.setdefault(spec.template, []).append(spec)

    for template, template_specs in grouped.items():
        columns = template_specs[0].columns
        lines.append(f"file {template} {{")
        lines.append("  pattern { " + ", ".join(columns) + " }")
        for spec in template_specs:
            row = ", ".join(f'"{spec.values[column]}"' for column in columns)
            lines.append(f"  {{ {row} }}")
        lines.append("}")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate substitutions for C++ logic EPICS exports")
    parser.add_argument("--logic-lib", required=True, help="Path to compiled C++ logic shared library")
    parser.add_argument("--output", required=True, help="Output substitutions file")
    parser.add_argument(
        "--param-prefix",
        default="",
        help="Optional asyn parameter prefix prepended to each exported variable name",
    )
    args = parser.parse_args()

    logic_name, exports = load_exports(Path(args.logic_lib))
    specs = [build_template_spec(args.param_prefix, export_var) for export_var in exports]
    Path(args.output).write_text(render_substitutions(logic_name, specs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
