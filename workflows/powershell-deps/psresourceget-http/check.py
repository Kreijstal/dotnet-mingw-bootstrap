"""Exercise the installed original module against an owned authenticated HTTP V3 feed.

Usage (MSYS Python): check.py MODULE_MANIFEST ORIGINAL_PUBLISHED_FIXTURES NEW_OUTPUT
The fixture nupkgs must come from the original Publish-PSResource lifecycle check.
"""
from pathlib import Path
import base64,hashlib,http.server,json,subprocess,sys,threading,urllib.parse,xml.etree.ElementTree as E,zipfile

manifest,fixtures,out=(Path(p).resolve() for p in sys.argv[1:]);out.mkdir()
packages={}
for path in fixtures.glob('*.nupkg'):
    with zipfile.ZipFile(path) as z:
        spec=E.fromstring(z.read(next(n for n in z.namelist() if n.endswith('.nuspec'))));meta=spec.find('{*}metadata')
        name,version=(meta.findtext('{*}'+key) for key in ['id','version'])
        packages.setdefault(name.lower(),[]).append(dict(name=name,version=version,data=path.read_bytes()))
for versions in packages.values():versions.sort(key=lambda r:tuple(int(x) for x in r['version'].split('.')))
dep=next(v[0] for k,v in packages.items() if k.startswith('owneddependency'))
main=next(v[-1] for k,v in packages.items() if k.startswith('ownedmodule'))
requests=[];counts=dict(challenges=0,authenticated=0,downloads=0,missing=0)
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        path=urllib.parse.urlparse(self.path).path
        authenticated=self.headers.get('Authorization')=='Basic '+base64.b64encode(b'owned-user:owned-fixture-password').decode()
        requests.append(dict(path=path,authenticated=authenticated))
        if not authenticated:
            counts['challenges']+=1
            self.send_response(401);self.send_header('WWW-Authenticate','Basic realm="owned-source-fixture"');self.send_header('Content-Length','0');self.end_headers();return
        counts['authenticated']+=1
        status=200;content_type='application/json'
        if path=='/v3/index.json':body=dict(version='3.0.0',resources=[{'@id':address+'registration/','@type':'RegistrationsBaseUrl/3.6.0'}])
        elif path.startswith('/registration/') and path.endswith('/index.json') and path.split('/')[2] in packages:
            versions=packages[path.split('/')[2]]
            entries=[]
            for r in versions:
                content=address+'flat/'+r['name'].lower()+'/'+r['version']+'/'+r['name'].lower()+'.'+r['version']+'.nupkg'
                catalog=dict(id=r['name'],version=r['version'],authors='owned',description='owned source λ package',tags=['PSModule'],listed=True,published='2026-01-01T00:00:00Z')
                entries.append(dict(catalogEntry=catalog,packageContent=content))
            body=dict(items=[dict(lower=versions[0]['version'],upper=versions[-1]['version'],items=entries)])
        elif path.startswith('/flat/') and len(path.split('/'))==5:
            _,_,name,version,filename=path.split('/')
            r=next((r for r in packages.get(name,[]) if r['version']==version and filename==name+'.'+version+'.nupkg'),None)
            if r:body=r['data'];content_type='application/octet-stream';counts['downloads']+=1
            else:status=404;body=b'';counts['missing']+=1
        else:status=404;body=b'';counts['missing']+=1
        if not isinstance(body,bytes):body=json.dumps(body,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type',content_type);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
address='http://127.0.0.1:'+str(server.server_address[1])+'/'
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
try:
    args=['/usr/bin/timeout','150','/clang64/bin/powershell-module-test-host',native(Path(__file__).with_name('Check.ps1')),native(manifest),native(out/'consumer'),address+'v3/index.json',dep['name'],main['name'],hashlib.sha256(dep['data']).hexdigest()]
    result=subprocess.run(args,check=False)
finally:
    server.shutdown();server.server_close();thread.join()
    (out/'requests.json').write_text(json.dumps(dict(counts=counts,requests=requests),indent=2)+'\n')
assert result.returncode==0,result.returncode
assert counts['challenges']>0 and counts['authenticated']>0 and counts['downloads']>=2 and counts['missing']>0,counts
print('PASS real owned HTTP requests, credential challenges, authenticated registration/downloads and404:',counts)
