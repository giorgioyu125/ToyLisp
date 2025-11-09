project('ToyLisp',
        'c',
        version: '0.2.0')


add_project_arguments('-ggdb', '-O1', language: 'c')

executable('ToyLisp',
    sources: ['src/toylisp.c', 'src/arena.c'],
    link_args: ['-lm'],
    install: true
)
