"""Testes de integracao. Usa somente a biblioteca padrao do Python 3.

Uso: python tests/test_outputs.py [caminho/do/vae.exe]
"""
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
DADOS = ROOT / "dados" / "digitos.txt"


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
    """Le cada aba: (grade de 48 valores, probabilidades ou [])."""
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


def read_data():
    return [[int(v) for v in line.split()] for line in DADOS.read_text().splitlines() if line.strip()]


def test_all():
    print(run("autoteste").strip())
    run(expected=2)
    run("desconhecido", expected=2)
    for args in [
        ("--a", "10"), ("--b", "-1"), ("--alpha", "1.1"), ("--alpha", "nan"),
        ("--passos", "1"), ("--epocas", "0"), ("--taxa", "0"), ("--beta", "-1"),
        ("--limiar", "1"), ("--semente", "0"), ("--semente", "4294967296"),
        ("--alpha", ".5", "--passos", "5"), ("--saida", "falso.csv"), ("--a",), ("--xyz", "1"),
    ]:
        run("misturar", *args, expected=2)

    data = read_data()
    assert len(data) == 10 and all(len(v) == 48 for v in data)

    with tempfile.TemporaryDirectory(prefix="vae_test_") as temp:
        folder = Path(temp)
        common = ("--dados", DADOS)

        # Dados de entrada
        bad = folder / "ruim.txt"
        bad.write_text("0 1 2\n")
        run("mostrar", "--dados", bad, expected=1)
        run("mostrar", "--dados", folder / "ausente.txt", expected=1)
        labeled = folder / "rotulados.txt"
        labeled.write_text("# comentario\n7 " + " ".join(map(str, data[7])) + "\n\n")
        assert "Exemplo 1: digito 7" in run("mostrar", "--dados", labeled)

        templates_path = folder / "digitos.xlsx"
        run("mostrar", *common, "--saida", templates_path)
        templates = read_book(templates_path, 10)
        assert [g for g, _ in templates] == data
        original_bytes = templates_path.read_bytes()
        run("mostrar", *common, "--saida", templates_path, expected=1)
        assert templates_path.read_bytes() == original_bytes, "Nao deve sobrescrever"
        run("mostrar", *common, "--saida", folder / "ausente" / "x.xlsx", expected=1)

        # Modelo
        model = folder / "modelo.bin"
        run("misturar", *common, "--modelo", model, expected=1)
        output = run("treinar", *common, "--modelo", model)
        assert "celulas erradas: 0/480" in output.splitlines()[-2], output
        corrupt = folder / "corrompido.bin"
        corrupt.write_bytes(model.read_bytes()[:-8])
        run("gerar", "--modelo", corrupt, expected=1)
        model_bytes = model.read_bytes()
        run("treinar", *common, "--modelo", folder / "modelo2.bin")
        assert (folder / "modelo2.bin").read_bytes() == model_bytes, "Semente fixa deve reproduzir o modelo"
        m = ("--modelo", model)

        # Reconstrucao
        rec = folder / "reconstrucao.xlsx"
        assert "Total: 0 de 480" in run("reconstruir", *common, *m, "--saida", rec)
        assert [g for g, _ in read_book(rec, 10)] == data

        # Mistura: extremos reconstroem A e B; probabilidades mudam a cada passo.
        mix = folder / "mistura.xlsx"
        run("misturar", *common, *m, "--a", 0, "--b", 8, "--passos", 5, "--saida", mix)
        sheets = read_book(mix, 5)
        assert sheets[0][0] == data[0]
        assert sheets[-1][0] == data[8]
        assert len({tuple(p) for _, p in sheets}) == 5
        repeat = folder / "repeticao.xlsx"
        run("misturar", *common, *m, "--a", 0, "--b", 8, "--passos", 5, "--saida", repeat)
        assert mix.read_bytes() == repeat.read_bytes(), "Mesmo modelo deve reproduzir o arquivo"
        single = folder / "unica.xlsx"
        run("misturar", *common, *m, "--a", 0, "--b", 8, "--alpha", .5, "--saida", single)
        assert read_book(single, 1)[0] == sheets[2]
        low = folder / "limiar.xlsx"
        run("misturar", *common, *m, "--a", 0, "--b", 8, "--alpha", .5, "--limiar", .3, "--saida", low)
        low_sheet = read_book(low, 1, threshold=.3)[0]
        assert low_sheet[1] == sheets[2][1]
        assert sum(low_sheet[0]) >= sum(sheets[2][0])
        noisy = folder / "ruido.xlsx"
        run("misturar", *common, *m, "--a", 0, "--b", 8, "--alpha", .5, "--ruido", .2, "--saida", noisy)
        assert read_book(noisy, 1)[0][1] != sheets[2][1]

        # Geracao a partir de z ~ N(0, I)
        gen = folder / "gerados.xlsx"
        run("gerar", *m, "--n", 7, "--saida", gen)
        generated = read_book(gen, 7)
        assert len({tuple(p) for _, p in generated}) == 7
        gen2 = folder / "gerados2.xlsx"
        run("gerar", *m, "--n", 7, "--semente", 7, "--saida", gen2)
        assert read_book(gen2, 7) != generated
    print("OK: argumentos, dados, modelo, ZIP/XML, reconstrucao, mistura, limiar, ruido, geracao e reproducibilidade.")


if __name__ == "__main__":
    test_all()
