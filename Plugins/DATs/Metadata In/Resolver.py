"""Host-thread-only path resolution. No operator-family allowlist or file I/O."""


def resolve(node, file_provided, operator_provided):
    # Presence comes from the SDK's evaluated string values, including unresolved
    # operator paths. It is also used by the adapter's parameter enable states.
    if file_provided and operator_provided:
        raise ValueError("File and Operator are both set; clear one to select a single source")
    if not file_provided and not operator_provided:
        return ""
    if operator_provided:
        target = node.par.Operator.eval()
        if isinstance(target, (list, tuple)):
            if len(target) != 1:
                raise ValueError("Select exactly one source operator")
            target = target[0]
        if target is None or not target.valid:
            raise ValueError("Select an existing source operator")
        if target == node:
            raise ValueError("The source operator cannot be MetaData_In itself")
        parameters = list(target.pars())
        matches = [p for p in parameters if p.name.lower() == "file"]
        if not matches:
            matches = [p for p in parameters if p.label.lower() == "file"]
        if len(matches) != 1:
            if not matches:
                raise ValueError("The selected operator has no File parameter")
            raise ValueError("The selected operator has multiple File parameters; its file source is ambiguous")
        parameter = matches[0]
    else:
        parameter = node.par.File
    # Inspect the evaluated value, never .val: expressions, binds and exports matter.
    value = parameter.eval()
    if not isinstance(value, str) or not value.strip():
        raise ValueError("Select one local file")
    if "\x00" in value:
        raise ValueError("File path contains a null character")
    lower = value.lower()
    if "://" in value or lower.startswith(("vfs:", "http:", "https:", "file:")):
        raise ValueError("Only local filesystem files are supported; URLs and VFS paths are not")
    # evalFile applies this parameter's owner's relative-path and project.paths rules.
    path = parameter.evalFile().absPath
    if not isinstance(path, str) or not path:
        raise ValueError("Could not resolve the File parameter to a local path")
    return path
