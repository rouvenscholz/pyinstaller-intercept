def edit_requests(module):
    def gen_fake_get(original_get):
        def fake_get(url, *args, **kwargs):
            print(f'requests.get("{url}", ...)')
            return original_get(url, *args, **kwargs)
        return fake_get
    module.get = gen_fake_get(module.get)
ModuleEdit.add_module_editor("requests", edit_requests)
