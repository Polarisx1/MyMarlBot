import json
import ctypes


def struct_to_dict(struct):
    result = {}
    for field, _ in struct._fields_:
        value = getattr(struct, field)
        # Convertir des objets ctypes ou des structures personnalisées en dictionnaires
        if hasattr(value, "_length_") and hasattr(value, "_type_"):
            # C'est un tableau de ctypes
            result[field] = [struct_to_dict(item) if hasattr(item, "_fields_") else item for item in value]
        elif hasattr(value, "_fields_"):
            # C'est une structure ctypes
            result[field] = struct_to_dict(value)
        else:
            # Pour les types de base ctypes
            result[field] = value.value if isinstance(value, ctypes._SimpleCData) else value
    return result

def serialize_to_json(packet):
    packet_dict = struct_to_dict(packet)
    return json.dumps(packet_dict, indent=4)


def move_cursor_up(lines):
   
    print(f"\033[{lines}A", end="")

def clear_line():

    print("\033[K", end="")
    
    
def clear_lines(lines):

    for _ in range(lines):
        clear_line()
        move_cursor_up(1)
   
    
    
def clear_screen():
     print("\033[2J\033[H", end="")


from pkg_resources import get_distribution, parse_version


def check_rlsdk_version(min_version: str = "0.4.2") -> str:
    """Ensure that the installed rlsdk-python meets the minimum version.

    Parameters
    ----------
    min_version : str
        Minimum required version string.

    Returns
    -------
    str
        The installed version of rlsdk-python.

    Raises
    ------
    RuntimeError
        If rlsdk-python is missing or the version is too old.
    """

    try:
        installed_version = get_distribution("rlsdk-python").version
    except Exception:
        raise RuntimeError(
            "rlsdk-python is not installed. Please install it with 'pip install rlsdk-python'."
        )

    if parse_version(installed_version) < parse_version(min_version):
        raise RuntimeError(
            f"rlsdk-python>={min_version} is required, but {installed_version} is installed."
        )

    return installed_version
