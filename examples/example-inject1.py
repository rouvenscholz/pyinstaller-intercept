def edit_datetime(module):
    class FakeDatetime(module.datetime):
        def now():
            return module.datetime.fromtimestamp(0)
    module.datetime = FakeDatetime
ModuleEdit.add_module_editor("datetime", edit_datetime)
