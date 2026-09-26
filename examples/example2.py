import requests
try:
    secret_api_endpoint = "secret_api_endpoint"
    url= "https://example.org/"
    print(f'open url {url}')
    requests.get(url + secret_api_endpoint)
except Exception:
    pass
