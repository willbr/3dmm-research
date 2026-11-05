"""MCP stdio frontend for an isolated, injected ISO retail test process."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import pathlib
from session import RetailSession, prepare_iso


def tool(name, description, properties=None, required=None):
    return {'name': name, 'description': description, 'inputSchema': {
        'type': 'object', 'properties': properties or {}, 'required': required or [],
        'additionalProperties': False}}


TOOLS = [
    tool('get_state', 'Read native scene, camera and actor state.',
         {'movie_details': {'type': 'boolean'}}),
    tool('seek_movie', 'Seek using one-based UI scene/frame numbers; return the actual native success result.',
         {'scene': {'type': 'integer', 'minimum': 1}, 'frame': {'type': 'integer', 'minimum': 1}},
         ['scene', 'frame']),
    tool('send_command', 'Queue an untargeted native Kauai command. Queued does not mean executed.',
         {name: {'type': 'integer'} for name in ('cid', 'lw0', 'lw1', 'lw2', 'lw3')}, ['cid']),
    tool('wait_ms', 'Allow the retail UI to run, for at most 30 seconds.',
         {'ms': {'type': 'integer', 'minimum': 0, 'maximum': 30000}}, ['ms']),
    tool('screenshot', 'Capture the retail client area as PNG on its UI thread.'),
]


def dispatch(session, message):
    method, params = message.get('method'), message.get('params', {})
    if method == 'initialize':
        return {'protocolVersion': '2024-11-05', 'capabilities': {'tools': {}},
                'serverInfo': {'name': '3dmm-retail-oracle', 'version': '1'}}
    if method == 'ping':
        return {}
    if method == 'tools/list':
        return {'tools': TOOLS}
    if method == 'tools/call':
        name, arguments = params.get('name'), params.get('arguments', {})
        schema = next((item['inputSchema'] for item in TOOLS if item['name'] == name), None)
        if not schema:
            raise ValueError('Unsupported retail tool: ' + str(name))
        if not isinstance(arguments, dict) or set(arguments) - schema['properties'].keys():
            raise ValueError('Unknown tool arguments')
        if set(schema['required']) - arguments.keys():
            raise ValueError('Missing tool arguments')
        for key, value in arguments.items():
            field = schema['properties'][key]
            if field['type'] == 'integer' and (type(value) is not int or not -2147483648 <= value <= 2147483647):
                raise ValueError(key + ' must be a signed 32-bit integer')
            if field['type'] == 'boolean' and type(value) is not bool:
                raise ValueError(key + ' must be boolean')
            if 'minimum' in field and value < field['minimum'] or 'maximum' in field and value > field['maximum']:
                raise ValueError(key + ' is out of range')
        try:
            return session.call(name, **arguments)
        except Exception as error:
            return {'isError': True, 'content': [{'type': 'text', 'text': str(error)}]}
    raise LookupError('Unsupported method: ' + str(method))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('iso', 'bridge', 'movie', 'output'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    product, manifest = prepare_iso(args.iso, args.output / 'iso-package')
    # Session data and logs never share the JSON-RPC output stream.
    import tempfile
    artifacts = pathlib.Path(tempfile.mkdtemp(prefix='session-', dir=args.output.resolve()))
    (artifacts / 'iso.json').write_text(json.dumps(manifest, indent=2))
    session = RetailSession(args.bridge, product, args.movie, artifacts)
    try:
        for line in sys.stdin:
            message = None
            try:
                message = json.loads(line)
                if not isinstance(message, dict) or message.get('jsonrpc') != '2.0':
                    raise ValueError('Expected JSON-RPC 2.0 object')
                if 'id' not in message:
                    continue
                response = {'jsonrpc': '2.0', 'id': message['id'], 'result': dispatch(session, message)}
            except Exception as error:
                response = {'jsonrpc': '2.0', 'id': message.get('id') if isinstance(message, dict) else None,
                            'error': {'code': -32601 if isinstance(error, LookupError) else -32602,
                                      'message': str(error)}}
            print(json.dumps(response), flush=True)
    finally:
        session.close()


if __name__ == '__main__':
    main()
