"""Testes de integracao. Usa somente a biblioteca padrao do Python 3."""
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

NS = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
ROOT = Path(__file__).resolve().parents[1]
EXE = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "vae.exe"


def run(*args, expected=0):
    result = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout


def column(index):
    result = ""
    while index >= 0:
        result = chr(65 + index % 26) + result
        index = index // 26 - 1
    return result


def read_book(path, count, threshold=0.5):
    result = []
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        for name in z.namelist():
            ET.fromstring(z.read(name))
        book = ET.fromstring(z.read("xl/workbook.xml"))
        assert len(book.find("s:sheets", NS)) == count
        for n in range(1, count + 1):
            tree = ET.fromstring(z.read(f"xl/worksheets/sheet{n}.xml"))
            cells = {c.attrib["r"]: c for c in tree.findall(".//s:c", NS)}
            grid, probs = [], []
            for i in range(48):
                ref = f"{column(i % 6)}{i // 6 + 1}"
                value = int(cells[ref].find("s:v", NS).text)
                assert value in (0, 1)
                grid.append(value)
                vector_cell = cells[f"{column(i + 7)}1"]
                assert vector_cell.find("s:f", NS).text == ref
                assert int(vector_cell.find("s:v", NS).text) == value
                prob_ref = f"{column(i % 6)}{i // 6 + 12}"
                if prob_ref in cells:
                    p = float(cells[prob_ref].find("s:v", NS).text)
                    assert math.isfinite(p) and 0 <= p <= 1
                    assert value == int(p >= threshold)
                    probs.append(p)
            result.append((grid, probs))
    return result


def test_all():
    print(run("--self-test").strip())
    for args in [
        ("--a", "10"), ("--b", "-1"), ("--alpha", "1.1"), ("--alpha", "nan"),
        ("--steps", "1"), ("--epochs", "0"), ("--rate", "0"), ("--beta", "-1"),
        ("--threshold", "1"), ("--seed", "0"), ("--seed", "4294967296"),
        ("--alpha", ".5", "--steps", "5"), ("--out", "falso.csv"), ("--a",),
    ]:
        run(*args, expected=2)
    with tempfile.TemporaryDirectory(prefix="vae_test_") as temp:
        folder = Path(temp)
        patterns = folder / "modelos.xlsx"
        run("--templates", patterns)
        templates = read_book(patterns, 10)
        expected_zero = "000000001100010010010010010010010010001100000000"
        assert "".join(map(str, templates[0][0])) == expected_zero
        assert len({tuple(g) for g, _ in templates}) == 10
        original_bytes = patterns.read_bytes()
        run("--templates", patterns, expected=1)
        assert patterns.read_bytes() == original_bytes
        run("--templates", folder / "ausente" / "teste.xlsx", expected=1)

        mix = folder / "mistura.xlsx"
        output = run("--a", 0, "--b", 8, "--steps", 5, "--out", mix)
        assert "erros nos modelos de treino: 0/480" in output
        sheets = read_book(mix, 5)
        assert sheets[0][0] == templates[0][0]
        assert sheets[-1][0] == templates[8][0]
        assert sheets[2][0] not in (templates[0][0], templates[8][0])
        assert len({tuple(p) for _, p in sheets}) == 5
        repeat = folder / "repeticao.xlsx"
        run("--a", 0, "--b", 8, "--steps", 5, "--out", repeat)
        assert mix.read_bytes() == repeat.read_bytes(), "Semente fixa deve reproduzir o arquivo"
        single = folder / "unica.xlsx"
        run("--a", 0, "--b", 8, "--alpha", .5, "--out", single)
        assert read_book(single, 1)[0] == sheets[2]
        low = folder / "limiar.xlsx"
        run("--a", 0, "--b", 8, "--alpha", .5, "--threshold", .3, "--out", low)
        low_sheet = read_book(low, 1, threshold=.3)[0]
        assert low_sheet[1] == sheets[2][1]
        assert sum(low_sheet[0]) >= sum(sheets[2][0])
        noisy = folder / "ruido.xlsx"
        run("--a", 0, "--b", 8, "--alpha", .5, "--noise", .2, "--out", noisy)
        assert read_book(noisy, 1)[0][1] != sheets[2][1]
    print("OK: argumentos, ZIP/XML, 48 formulas por aba, desenhos, probabilidades, limiar, endpoints, mistura e reproducibilidade.")


if __name__ == "__main__":
    test_all()
