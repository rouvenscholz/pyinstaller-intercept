class ModuleEdit():
    from sys import meta_path
    _module_editors = {}
    _print_imports = False


    def add_module_editor(module_name, editor):
        __class__._do_init()
        __class__._module_editors[module_name] = {
            "editor": editor,
            "edited": False
        }


    def print_imports(print_imports):
        __class__._do_init()
        __class__._print_imports = print_imports


    def _do_init():
        if not hasattr(__class__, "_initialized"):
            setattr(__class__, "_initialized", True)
            __class__.meta_path.insert(0, __class__.FakeMetaPathFinder)


    from importlib.abc import Loader
    class LoaderWrapper(Loader):
        def __init__(self, orig_loader, module_editor_obj):
            self.orig_loader = orig_loader
            self.module_editor_obj = module_editor_obj

        def create_module(self, spec):
            res = self.orig_loader.create_module(spec)
            return res

        def exec_module(self, module):
            res = self.orig_loader.exec_module(module)
            if not self.module_editor_obj["edited"]:
                self.module_editor_obj["edited"] = True
                self.module_editor_obj["editor"](module)
            return res

    from importlib.abc import MetaPathFinder
    class FakeMetaPathFinder(MetaPathFinder):
        def find_spec(fullname, path, target=None):
            if ModuleEdit._print_imports:
                print(f'import {fullname}')
            if module_editor_obj := ModuleEdit._module_editors.get(fullname, None):
                for metaPathFinder in ModuleEdit.meta_path:
                    if metaPathFinder != __class__:
                        if module_spec := metaPathFinder.find_spec(fullname, path, target=target):
                            module_spec.loader = ModuleEdit.LoaderWrapper(module_spec.loader, module_editor_obj)
                            return module_spec
