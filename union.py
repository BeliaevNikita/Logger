import re

def load_logs(log_file):
    """
    Загружает логи: key = текст инструкции, value = список значений.
    """
    instr_vals = {}
    with open(log_file, "r") as f:
        for line in f:
            m = re.match(r".*?\]\s*(.+?)\s+val=(-?\d+)", line.strip())
            if m:
                instr = m.group(1).strip()
                val = m.group(2).strip()
                instr_vals.setdefault(instr, []).append(val)
    return instr_vals


def load_defuse(defuse_file):
    """
    Загружает список def-use рёбер: [(instr1, instr2), ...]
    """
    edges = []
    with open(defuse_file, "r") as f:
        for line in f:
            m = re.match(r'\s*"?(.*?)"?\s*->\s*"?(.*?)"?;', line.strip())
            if m:
                edges.append((m.group(1).strip(), m.group(2).strip()))
    return edges


def merge_cfg_with_logs_and_defuse(cfg_file, log_file, defuse_file, out_file):
    logs = load_logs(log_file)
    defuse_edges = load_defuse(defuse_file)

    with open(cfg_file, "r") as f:
        lines = f.readlines()

    new_lines = []
    node_re = re.compile(r'(\s*".+?"\s*\[label=")(.+)("\];)')

    # Словарь: label -> node_id (адрес)
    label_to_id = {}

    for line in lines:
        m = node_re.match(line)
        if m:
            node_id = re.match(r'\s*"(.*?)"', line).group(1)
            instr_text = m.group(2).strip()

            label = instr_text
            if instr_text in logs:
                vals = ",".join(logs[instr_text])
                label = f"{instr_text} | val=[{vals}]"

            new_line = f'{m.group(1)}{label}{m.group(3)}\n'
            new_lines.append(new_line)

            label_to_id[instr_text] = node_id
        else:
            new_lines.append(line)

    # Добавляем def-use рёбра
    for src, dst in defuse_edges:
        if src in label_to_id and dst in label_to_id:
            src_id = label_to_id[src]
            dst_id = label_to_id[dst]
            new_lines.insert(-1, f'  "{src_id}" -> "{dst_id}" [color=red, label="use"];\n')

    with open(out_file, "w") as f:
        f.writelines(new_lines)


if __name__ == "__main__":
    merge_cfg_with_logs_and_defuse("cfg.dot", "log.txt", "defuse.dot", "cfg_with_vals.dot")
    print("Новый граф сохранён в cfg_with_vals.dot")
