#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path


DEFAULT_FIXTURE_DIR = Path("tests/ast_json")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_cmd(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def parse_last_json(text: str):
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    require(lines, "JSON output is empty")
    return json.loads(lines[-1])


def orhun_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def is_error_fixture(path: Path) -> bool:
    try:
        return path.read_text(encoding="utf-8").splitlines()[0].strip() == "# ast-error"
    except IndexError:
        return False


def cxx_parse_payload(binary: Path, repo: Path, source_file: Path, should_fail: bool) -> dict:
    proc = run_cmd([str(binary), "parse", str(source_file), "--json"], repo)
    if should_fail:
        require(
            proc.returncode == 1,
            f"C++ parse should fail for {source_file}:\n"
            f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
        )
        return parse_last_json(proc.stdout)

    require(
        proc.returncode == 0,
        f"C++ parse failed for {source_file}:\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
    )
    return parse_last_json(proc.stdout)


def error_line_from_cxx(payload: dict, source_file: Path) -> int:
    message = error_message(payload, source_file)
    match = re.search(r"Satır\s+(\d+)", message)
    require(
        match is not None,
        f"C++ error missing line number for {source_file}: {message}",
    )
    return int(match.group(1))


def error_message(payload: dict, source_file: Path) -> str:
    error = payload.get("hata")
    require(isinstance(error, dict), f"parse payload missing hata for {source_file}")
    message = error.get("mesaj")
    require(
        isinstance(message, str) and message,
        f"parse payload missing hata.mesaj for {source_file}",
    )
    return message


def expected_hint(message: str) -> str:
    match = re.search(r"'([^']+)'\s+bekleniyor", message)
    if match is not None:
        return match.group(1)
    return ""


def unknown_command(message: str) -> str:
    match = re.search(r"Tanınmayan komut:\s+'([^']+)'", message)
    if match is not None:
        return match.group(1)
    return ""


def suggested_command(message: str) -> str:
    match = re.search(r"Bunu mu demek istediniz:\s+'([^']+)'", message)
    if match is not None:
        return match.group(1)
    return ""


def validate_error_parity(
    cxx_payload: dict, proto_payload: dict, source_file: Path
) -> None:
    proto_error = proto_payload.get("hata")
    require(
        isinstance(proto_error, dict),
        f"prototype payload missing hata for {source_file}",
    )
    require(
        proto_error.get("satir") == error_line_from_cxx(cxx_payload, source_file),
        f"Parser error line mismatch for {source_file}\n"
        f"C++: {cxx_payload}\nOrhun: {proto_payload}",
    )

    cxx_hint = expected_hint(error_message(cxx_payload, source_file))
    proto_message = proto_error.get("mesaj")
    require(
        isinstance(proto_message, str) and proto_message,
        f"prototype payload missing hata.mesaj for {source_file}",
    )
    proto_hint = expected_hint(proto_message)
    require(
        cxx_hint == proto_hint,
        f"Parser error hint mismatch for {source_file}\n"
        f"C++: {cxx_hint}\nOrhun: {proto_hint}",
    )

    cxx_message = error_message(cxx_payload, source_file)
    cxx_unknown = unknown_command(cxx_message)
    proto_unknown = unknown_command(proto_message)
    require(
        cxx_unknown == proto_unknown,
        f"Parser unknown command mismatch for {source_file}\n"
        f"C++: {cxx_unknown}\nOrhun: {proto_unknown}",
    )

    cxx_suggestion = suggested_command(cxx_message)
    proto_suggestion = suggested_command(proto_message)
    require(
        cxx_suggestion == proto_suggestion,
        f"Parser suggestion mismatch for {source_file}\n"
        f"C++: {cxx_suggestion}\nOrhun: {proto_suggestion}",
    )


def cxx_top_level_nodes(payload: dict, source_file: Path) -> list[dict]:
    ast = payload.get("ast")
    require(isinstance(ast, dict), f"C++ parse payload missing ast for {source_file}")
    commands = ast.get("komutlar")
    require(isinstance(commands, list), f"C++ AST missing komutlar for {source_file}")
    return [cxx_node_summary(command) for command in commands]


def cxx_node_summary(command: dict) -> dict:
    blocks = cxx_block_summaries(command)
    expression = cxx_command_expression_summary(command)
    summary = {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
        "blok_sayilari": block_counts(blocks),
        "bloklar": blocks,
    }
    add_assignment_metadata(summary, command, "C++")
    add_definition_metadata(summary, command, "C++")
    add_control_metadata(summary, command, "C++")
    return summary


def cxx_shallow_node(command: dict) -> dict:
    expression = cxx_command_expression_summary(command)
    summary = {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
    }
    add_assignment_metadata(summary, command, "C++")
    add_definition_metadata(summary, command, "C++")
    add_control_metadata(summary, command, "C++")
    return summary


def add_assignment_metadata(summary: dict, command: dict, source_name: str) -> None:
    if summary.get("tur") not in {"Atama", "CokluAtama"}:
        return
    marker = command.get("bildirim")
    require(
        isinstance(marker, bool),
        f"{source_name} assignment node missing bool bildirim: {command}",
    )
    summary["bildirim"] = marker
    if summary.get("tur") == "Atama":
        target = command.get("hedef") or command.get("hedef_ozeti")
        require(
            isinstance(target, dict),
            f"{source_name} assignment node missing target summary: {command}",
        )
        if "tur" in target and "altlar" in target:
            summary["hedef_ozeti"] = orhun_expression_payload(target, Path(source_name))
        else:
            summary["hedef_ozeti"] = cxx_expression_summary(target)
    if summary.get("tur") == "CokluAtama":
        targets = command.get("hedefler")
        require(
            isinstance(targets, list)
            and all(isinstance(target, str) for target in targets),
            f"{source_name} multi-assignment node missing target names: {command}",
        )
        summary["hedefler"] = targets


def expression_metadata_summary(expression: object, source_name: str) -> dict:
    require(
        isinstance(expression, dict),
        f"{source_name} control expression summary missing: {expression}",
    )
    if "tur" in expression and "altlar" in expression:
        return orhun_expression_payload(expression, Path(source_name))
    return cxx_expression_summary(expression)


def add_control_metadata(summary: dict, command: dict, source_name: str) -> None:
    if summary.get("tur") in {"Eger", "Surece"}:
        expression = command.get("kosul") or command.get("kosul_ozeti")
        summary["kosul_ozeti"] = expression_metadata_summary(expression, source_name)
    if summary.get("tur") == "Tekrarla":
        expression = command.get("kac_kez") or command.get("kac_kez_ozeti")
        summary["kac_kez_ozeti"] = expression_metadata_summary(expression, source_name)


def add_definition_metadata(summary: dict, command: dict, source_name: str) -> None:
    if summary.get("tur") == "IslevTanim":
        name = command.get("ad")
        params = command.get("parametreler")
        defaults = command.get("varsayilanlar")
        require(
            isinstance(name, str),
            f"{source_name} function definition missing name: {command}",
        )
        require(
            isinstance(params, list)
            and all(isinstance(param, str) for param in params),
            f"{source_name} function definition missing params: {command}",
        )
        require(
            isinstance(defaults, list),
            f"{source_name} function definition missing defaults: {command}",
        )
        summary["ad"] = name
        summary["parametreler"] = params
        normalized_defaults = [
            definition_default_summary(default, source_name) for default in defaults
        ]
        summary["varsayilanlar"] = normalized_defaults
        summary["parametre_sayisi"] = metadata_count(
            command, "parametre_sayisi", len(params), source_name, "function definition"
        )
        summary["varsayilan_sayisi"] = metadata_count(
            command,
            "varsayilan_sayisi",
            default_value_count(normalized_defaults),
            source_name,
            "function definition",
        )
    if summary.get("tur") == "SinifTanim":
        name = command.get("ad")
        parent = command.get("ebeveyn")
        require(
            isinstance(name, str),
            f"{source_name} class definition missing name: {command}",
        )
        require(
            isinstance(parent, str),
            f"{source_name} class definition missing parent: {command}",
        )
        summary["ad"] = name
        summary["ebeveyn"] = parent
    if summary.get("tur") == "DisIslevTanim":
        name = command.get("ad")
        library = command.get("kutuphane")
        param_names = command.get("parametre_adlari")
        param_types = command.get("parametre_tipleri")
        return_type = command.get("donus_tipi")
        require(
            isinstance(name, str),
            f"{source_name} external function definition missing name: {command}",
        )
        require(
            isinstance(library, str),
            f"{source_name} external function definition missing library: {command}",
        )
        require(
            isinstance(param_names, list)
            and all(isinstance(param, str) for param in param_names),
            f"{source_name} external function definition missing param names: {command}",
        )
        require(
            isinstance(param_types, list)
            and all(isinstance(param, str) for param in param_types),
            f"{source_name} external function definition missing param types: {command}",
        )
        require(
            isinstance(return_type, str),
            f"{source_name} external function definition missing return type: {command}",
        )
        summary["ad"] = name
        summary["kutuphane"] = library
        summary["parametre_adlari"] = param_names
        summary["parametre_tipleri"] = param_types
        summary["parametre_sayisi"] = metadata_count(
            command,
            "parametre_sayisi",
            len(param_names),
            source_name,
            "external function definition",
        )
        summary["donus_tipi"] = return_type
    if summary.get("tur") == "DahilEt":
        source = command.get("dosya")
        require(
            isinstance(source, str),
            f"{source_name} include command missing source: {command}",
        )
        summary["dosya"] = source
    if summary.get("tur") == "DenemeYakala":
        error_name = command.get("hata_degiskeni")
        require(
            isinstance(error_name, str),
            f"{source_name} try/catch command missing error variable: {command}",
        )
        summary["hata_degiskeni"] = error_name


def definition_default_summary(default: object, source_name: str) -> dict:
    if default is None:
        return empty_expression_summary()
    require(
        isinstance(default, dict),
        f"{source_name} function default expression invalid: {default}",
    )
    if "tur" in default and "altlar" in default:
        return orhun_expression_payload(default, Path(source_name))
    return cxx_expression_summary(default)


def default_value_count(defaults: list[dict]) -> int:
    return sum(1 for default in defaults if default.get("tur"))


def metadata_count(
    owner: dict, key: str, fallback: int, source_name: str, label: str
) -> int:
    if key not in owner:
        require(
            not source_name.startswith("prototype "),
            f"{source_name} {label} missing {key}: {owner}",
        )
        return fallback
    value = owner.get(key)
    require(isinstance(value, int), f"{source_name} {label} {key} invalid: {owner}")
    require(
        value == fallback,
        f"{source_name} {label} {key} mismatch: {value} != {fallback}",
    )
    return value


def cxx_command_expression_summary(command: dict) -> dict:
    for key in ("ifade", "kosul", "kac_kez"):
        expression = command.get(key)
        if isinstance(expression, dict):
            return cxx_expression_summary(expression)
    return empty_expression_summary()


def empty_expression_summary() -> dict:
    return {"tur": "", "op": "", "ayrinti": "", "altlar": []}


def cxx_expression_summary(expression: dict) -> dict:
    kind = expression.get("tur")
    if not isinstance(kind, str):
        return empty_expression_summary()
    summary = {
        "tur": kind,
        "op": expression.get("op", ""),
        "ayrinti": cxx_expression_detail(expression),
        "altlar": cxx_expression_children(expression),
    }
    add_expression_metadata(summary, expression, "C++")
    return summary


def cxx_expression_children(expression: dict) -> list[dict]:
    if expression.get("tur") == "Sor":
        child = expression.get("ifade")
        if isinstance(child, dict):
            return [cxx_expression_summary(child)]
    if expression.get("tur") == "TekliIslem":
        child = expression.get("ifade")
        if isinstance(child, dict):
            return [cxx_expression_summary(child)]
    if expression.get("tur") == "IkiliIslem":
        return [
            cxx_expression_summary(child)
            for child in (expression.get("sol"), expression.get("sag"))
            if isinstance(child, dict)
        ]
    if expression.get("tur") == "Liste":
        items = expression.get("ogeler")
        if isinstance(items, list):
            return [cxx_expression_summary(item) for item in items if isinstance(item, dict)]
    if expression.get("tur") == "Sozluk":
        items = expression.get("ogeler")
        if isinstance(items, list):
            return [
                cxx_expression_summary(item.get("deger"))
                for item in items
                if isinstance(item, dict) and isinstance(item.get("deger"), dict)
            ]
    if expression.get("tur") == "ListeUretec":
        return [
            cxx_expression_summary(child)
            for child in (expression.get("ifade"), expression.get("kaynak"), expression.get("kosul"))
            if isinstance(child, dict)
        ]
    if expression.get("tur") == "IndeksErisim":
        return [
            cxx_expression_summary(child)
            for child in (expression.get("hedef"), expression.get("indeks"))
            if isinstance(child, dict)
        ]
    if expression.get("tur") == "DilimErisim":
        return [
            cxx_expression_summary(child)
            for child in (
                expression.get("hedef"),
                expression.get("baslangic"),
                expression.get("bitis"),
            )
            if isinstance(child, dict)
        ]
    if expression.get("tur") == "AlanErisim":
        target = expression.get("hedef")
        if isinstance(target, dict):
            return [cxx_expression_summary(target)]
    if expression.get("tur") == "GuvenliAlanErisim":
        target = expression.get("hedef")
        if isinstance(target, dict):
            return [cxx_expression_summary(target)]
    if expression.get("tur") == "YeniNesne":
        args = expression.get("argumanlar")
        if isinstance(args, list):
            return [cxx_expression_summary(arg) for arg in args if isinstance(arg, dict)]
    if expression.get("tur") == "IslevCagri":
        args = expression.get("argumanlar")
        if isinstance(args, list):
            return [cxx_expression_summary(arg) for arg in args if isinstance(arg, dict)]
    if expression.get("tur") == "IsimsizIslev":
        body = expression.get("govde")
        if isinstance(body, dict):
            commands = body.get("komutlar")
            if isinstance(commands, list):
                return [
                    cxx_expression_summary(command.get("ifade"))
                    for command in commands
                    if isinstance(command, dict)
                    and command.get("tur") == "Dondur"
                    and isinstance(command.get("ifade"), dict)
                ]
    return []


def cxx_expression_detail(expression: dict) -> str:
    kind = expression.get("tur")
    if kind in ("IkiliIslem", "TekliIslem"):
        return str(expression.get("op", ""))
    if kind in ("Sayi", "Ondalik", "Metin"):
        return str(expression.get("deger", ""))
    if kind == "Mantik":
        value = expression.get("deger")
        if value is True:
            return "doğru"
        if value is False:
            return "yanlış"
        return ""
    if kind == "Kimlik":
        return str(expression.get("ad", ""))
    if kind in ("GuvenliAlanErisim", "BenimErisim"):
        return str(expression.get("alan", ""))
    if kind == "UstErisim":
        return str(expression.get("metod", ""))
    if kind == "IndeksErisim":
        target = expression.get("hedef")
        if isinstance(target, dict):
            return str(target.get("ad", ""))
    if kind == "DilimErisim":
        target = expression.get("hedef")
        if isinstance(target, dict):
            return str(target.get("ad", ""))
    if kind == "AlanErisim":
        return str(expression.get("alan", ""))
    if kind == "YeniNesne":
        return str(expression.get("sinif", ""))
    if kind == "IslevCagri":
        return str(expression.get("ad", ""))
    if kind == "DahilEt":
        return str(expression.get("dosya", ""))
    if kind == "IsimsizIslev":
        params = expression.get("parametreler")
        if isinstance(params, list):
            return ",".join(str(param) for param in params)
    return ""


def cxx_block_summaries(command: dict) -> list[dict]:
    blocks = []
    for key in ("dogru_blok", "yanlis_blok", "govde", "deneme", "yakala"):
        block = command.get(key)
        if isinstance(block, dict):
            commands = block.get("komutlar")
            if isinstance(commands, list):
                blocks.append({"komutlar": [cxx_node_summary(child) for child in commands]})
    return blocks


def block_counts(blocks: list[dict]) -> list[int]:
    return [len(block["komutlar"]) for block in blocks]


def orhun_parser_payload(binary: Path, repo: Path, source_file: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="orhun_parser_proto_") as tmp:
        driver = Path(tmp) / "driver.oh"
        source_path = source_file.resolve().as_posix()
        driver.write_text(
            'parser olsun dahil_et "orhun/parser.oh"\n'
            f'kaynak olsun dosya.oku("{orhun_string(source_path)}")\n'
            "sonuc olsun parser.ozetle(kaynak)\n"
            "yazdır json.yaz(sonuc)\n",
            encoding="utf-8",
            newline="\n",
        )
        proc = run_cmd([str(binary), str(driver)], repo)
        require(
            proc.returncode == 0,
            f"Orhun parser prototype failed for {source_file}:\n"
            f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
        )
        payload = parse_last_json(proc.stdout)
        return payload


def orhun_parser_nodes(payload: dict, source_file: Path) -> list[dict]:
    require(payload.get("ok") is True, f"prototype returned error for {source_file}: {payload}")
    commands = payload.get("komutlar")
    require(isinstance(commands, list), f"prototype payload missing komutlar for {source_file}")
    return [orhun_node_summary(command, source_file) for command in commands]


def orhun_node_summary(command: dict, source_file: Path) -> dict:
    blocks = orhun_block_summaries(command.get("bloklar", []), source_file)
    counts = command.get("blok_sayilari", [])
    require(counts == block_counts(blocks), f"prototype block counts mismatch for {source_file}")
    expression = orhun_expression_summary(command, source_file)
    summary = {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
        "blok_sayilari": counts,
        "bloklar": blocks,
    }
    add_assignment_metadata(summary, command, f"prototype {source_file}")
    add_definition_metadata(summary, command, f"prototype {source_file}")
    add_control_metadata(summary, command, f"prototype {source_file}")
    return summary


def orhun_expression_summary(command: dict, source_file: Path) -> dict:
    expression = command.get("ifade_ozeti")
    require(isinstance(expression, dict), f"prototype expression summary missing for {source_file}")
    kind = expression.get("tur", "")
    require(command.get("ifade_turu", "") == kind, f"prototype expression kind mismatch for {source_file}")
    children = expression.get("altlar", [])
    require(isinstance(children, list), f"prototype expression children invalid for {source_file}")
    summary = {
        "tur": kind,
        "op": expression.get("op", ""),
        "ayrinti": expression.get("ayrinti", ""),
        "altlar": [orhun_expression_payload(child, source_file) for child in children],
    }
    add_expression_metadata(summary, expression, f"prototype {source_file}")
    return summary


def orhun_expression_payload(expression: object, source_file: Path) -> dict:
    require(isinstance(expression, dict), f"prototype child expression invalid for {source_file}")
    children = expression.get("altlar", [])
    require(isinstance(children, list), f"prototype child expression children invalid for {source_file}")
    summary = {
        "tur": expression.get("tur", ""),
        "op": expression.get("op", ""),
        "ayrinti": expression.get("ayrinti", ""),
        "altlar": [orhun_expression_payload(child, source_file) for child in children],
    }
    add_expression_metadata(summary, expression, f"prototype {source_file}")
    return summary


def add_expression_metadata(summary: dict, expression: dict, source_name: str) -> None:
    if summary.get("tur") == "IsimsizIslev":
        params = expression.get("parametreler")
        defaults = expression.get("varsayilanlar")
        require(
            isinstance(params, list) and all(isinstance(param, str) for param in params),
            f"{source_name} anonymous function expression missing params: {expression}",
        )
        require(
            isinstance(defaults, list),
            f"{source_name} anonymous function expression missing defaults: {expression}",
        )
        summary["parametreler"] = params
        normalized_defaults = [
            definition_default_summary(default, source_name) for default in defaults
        ]
        summary["varsayilanlar"] = normalized_defaults
        summary["parametre_sayisi"] = metadata_count(
            expression,
            "parametre_sayisi",
            len(params),
            source_name,
            "anonymous function expression",
        )
        summary["varsayilan_sayisi"] = metadata_count(
            expression,
            "varsayilan_sayisi",
            default_value_count(normalized_defaults),
            source_name,
            "anonymous function expression",
        )
    if summary.get("tur") == "ListeUretec":
        variable = expression.get("degisken")
        require(
            isinstance(variable, str),
            f"{source_name} list comprehension expression missing variable: {expression}",
        )
        summary["degisken"] = variable
        if "kosul_var" in expression:
            condition_present = expression.get("kosul_var")
        else:
            condition_present = expression.get("kosul") is not None
        require(
            isinstance(condition_present, bool),
            f"{source_name} list comprehension expression missing condition presence: {expression}",
        )
        summary["kosul_var"] = condition_present
    if summary.get("tur") == "Liste":
        items = expression.get("ogeler")
        if items is None:
            item_count = expression.get("oge_sayisi")
        else:
            require(
                isinstance(items, list),
                f"{source_name} list expression items invalid: {expression}",
            )
            item_count = len(items)
        require(
            isinstance(item_count, int),
            f"{source_name} list expression missing item count: {expression}",
        )
        summary["oge_sayisi"] = item_count
    if summary.get("tur") == "Sozluk":
        items = expression.get("ogeler")
        if items is None:
            keys = expression.get("anahtarlar")
            require(
                isinstance(keys, list) and all(isinstance(key, str) for key in keys),
                f"{source_name} dictionary expression missing keys: {expression}",
            )
            summary["anahtarlar"] = keys
            item_count = expression.get("oge_sayisi")
            require(
                isinstance(item_count, int),
                f"{source_name} dictionary expression missing item count: {expression}",
            )
            summary["oge_sayisi"] = item_count
            return
        require(
            isinstance(items, list),
            f"{source_name} dictionary expression items invalid: {expression}",
        )
        summary["anahtarlar"] = [
            str(item.get("anahtar", ""))
            for item in items
            if isinstance(item, dict)
        ]
        summary["oge_sayisi"] = len(items)
    if summary.get("tur") == "DilimErisim":
        if "baslangic_var" in expression and "bitis_var" in expression:
            start_present = expression.get("baslangic_var")
            end_present = expression.get("bitis_var")
        else:
            start_present = expression.get("baslangic") is not None
            end_present = expression.get("bitis") is not None
        require(
            isinstance(start_present, bool) and isinstance(end_present, bool),
            f"{source_name} slice expression missing bound presence: {expression}",
        )
        summary["baslangic_var"] = start_present
        summary["bitis_var"] = end_present
    if summary.get("tur") in {"AlanErisim", "GuvenliAlanErisim", "BenimErisim"}:
        field_name = expression.get("alan")
        require(
            isinstance(field_name, str),
            f"{source_name} field access expression missing field name: {expression}",
        )
        summary["alan"] = field_name
    if summary.get("tur") == "UstErisim":
        method_name = expression.get("metod")
        require(
            isinstance(method_name, str),
            f"{source_name} super access expression missing method name: {expression}",
        )
        summary["metod"] = method_name
    if summary.get("tur") == "ParalelYap":
        body = expression.get("govde")
        if body is None:
            command_count = expression.get("komut_sayisi")
        else:
            require(isinstance(body, dict), f"{source_name} parallel body invalid: {expression}")
            commands = body.get("komutlar")
            require(isinstance(commands, list), f"{source_name} parallel commands invalid: {expression}")
            command_count = len(commands)
        require(
            isinstance(command_count, int),
            f"{source_name} parallel command count missing: {expression}",
        )
        summary["komut_sayisi"] = command_count
    if summary.get("tur") in {"YeniNesne", "IslevCagri"}:
        if summary.get("tur") == "YeniNesne":
            class_name = expression.get("sinif")
            require(
                isinstance(class_name, str),
                f"{source_name} new-object expression missing class name: {expression}",
            )
            summary["sinif"] = class_name
        if summary.get("tur") == "IslevCagri":
            call_name = expression.get("ad")
            require(
                isinstance(call_name, str),
                f"{source_name} call expression missing name: {expression}",
            )
            summary["ad"] = call_name
        args = expression.get("argumanlar")
        if args is None:
            argument_count = expression.get("arguman_sayisi")
        else:
            require(
                isinstance(args, list),
                f"{source_name} call/new expression args invalid: {expression}",
            )
            argument_count = len(args)
        require(
            isinstance(argument_count, int),
            f"{source_name} call/new expression arg count missing: {expression}",
        )
        summary["arguman_sayisi"] = argument_count


def orhun_block_summaries(blocks: object, source_file: Path) -> list[dict]:
    require(isinstance(blocks, list), f"prototype payload has invalid bloklar for {source_file}")
    normalized = []
    for block in blocks:
        require(isinstance(block, dict), f"prototype block is not an object for {source_file}")
        commands = block.get("komutlar")
        require(isinstance(commands, list), f"prototype block missing komutlar for {source_file}")
        normalized.append(
            {
                "komutlar": [
                    orhun_node_summary(command, source_file)
                    for command in commands
                ]
            }
        )
    return normalized


def orhun_shallow_node(command: dict, source_file: Path) -> dict:
    expression = orhun_expression_summary(command, source_file)
    summary = {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
    }
    add_assignment_metadata(summary, command, f"prototype {source_file}")
    add_definition_metadata(summary, command, f"prototype {source_file}")
    add_control_metadata(summary, command, f"prototype {source_file}")
    return summary


def empty_coverage() -> dict[str, int]:
    return {
        "list_comp_condition_true": 0,
        "list_comp_condition_false": 0,
        "list_expressions": 0,
        "dict_expressions": 0,
        "function_definitions": 0,
        "external_function_definitions": 0,
        "anonymous_function_expressions": 0,
    }


def add_coverage(target: dict[str, int], source: dict[str, int]) -> None:
    for key, value in source.items():
        target[key] = target.get(key, 0) + value


def collect_expression_coverage(expression: dict, coverage: dict[str, int]) -> None:
    if expression.get("tur") == "ListeUretec":
        if expression.get("kosul_var") is True:
            coverage["list_comp_condition_true"] += 1
        elif expression.get("kosul_var") is False:
            coverage["list_comp_condition_false"] += 1
    elif expression.get("tur") == "Liste":
        coverage["list_expressions"] += 1
    elif expression.get("tur") == "Sozluk":
        coverage["dict_expressions"] += 1
    elif expression.get("tur") == "IsimsizIslev":
        coverage["anonymous_function_expressions"] += 1

    children = expression.get("altlar", [])
    if isinstance(children, list):
        for child in children:
            if isinstance(child, dict):
                collect_expression_coverage(child, coverage)


def collect_node_coverage(node: dict, coverage: dict[str, int]) -> None:
    if node.get("tur") == "IslevTanim":
        coverage["function_definitions"] += 1
    elif node.get("tur") == "DisIslevTanim":
        coverage["external_function_definitions"] += 1

    for key in ("ifade_ozeti", "hedef_ozeti"):
        expression = node.get(key)
        if isinstance(expression, dict):
            collect_expression_coverage(expression, coverage)

    defaults = node.get("varsayilanlar", [])
    if isinstance(defaults, list):
        for default in defaults:
            if isinstance(default, dict):
                collect_expression_coverage(default, coverage)

    blocks = node.get("bloklar", [])
    if isinstance(blocks, list):
        for block in blocks:
            if not isinstance(block, dict):
                continue
            commands = block.get("komutlar", [])
            if isinstance(commands, list):
                for command in commands:
                    if isinstance(command, dict):
                        collect_node_coverage(command, coverage)


def coverage_from_nodes(nodes: list[dict]) -> dict[str, int]:
    coverage = empty_coverage()
    for node in nodes:
        collect_node_coverage(node, coverage)
    return coverage


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare Orhun parser prototype summaries against C++ AST"
    )
    parser.add_argument("binary", help="Orhun executable path")
    parser.add_argument(
        "--fixtures",
        default=str(DEFAULT_FIXTURE_DIR),
        help="Directory containing AST JSON .oh fixtures",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    fixture_dir = (repo / args.fixtures).resolve()
    cases = sorted(fixture_dir.glob("*.oh"))
    require(cases, f"No parser prototype fixtures found under {fixture_dir}")

    ok_count = 0
    error_count = 0
    coverage = empty_coverage()
    for case in cases:
        should_fail = is_error_fixture(case)
        cxx_payload = cxx_parse_payload(binary, repo, case, should_fail)
        proto_payload = orhun_parser_payload(binary, repo, case)

        if should_fail:
            require(cxx_payload.get("durum") == "fail", f"C++ payload should fail for {case}")
            require(proto_payload.get("ok") is False, f"prototype should fail for {case}")
            validate_error_parity(cxx_payload, proto_payload, case)
            error_count += 1
            continue

        cxx = cxx_top_level_nodes(cxx_payload, case)
        proto = orhun_parser_nodes(proto_payload, case)
        require(
            cxx == proto,
            f"Parser prototype node mismatch for {case}\nC++: {cxx}\nOrhun: {proto}",
        )
        add_coverage(coverage, coverage_from_nodes(cxx))
        ok_count += 1

    print(
        "Parser prototype smoke passed "
        f"({ok_count} fixture ok, {error_count} fixture error, "
        "list-comp condition true/false: "
        f"{coverage['list_comp_condition_true']}/{coverage['list_comp_condition_false']}, "
        "collections list/dict: "
        f"{coverage['list_expressions']}/{coverage['dict_expressions']}, "
        "signatures function/external/anonymous: "
        f"{coverage['function_definitions']}/"
        f"{coverage['external_function_definitions']}/"
        f"{coverage['anonymous_function_expressions']})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
