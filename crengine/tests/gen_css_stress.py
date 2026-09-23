#!/usr/bin/env python3
"""Generate synthetic CSS stress documents for benchmarking style application.

Build Tools/bench_pages and run it on a generated document: a cold run
(no document cache) times LoadDocument, where styles are applied.

Each profile is a different selector mix, so A/B comparisons can be
attributed to specific selector categories:

  class-leading     every rightmost compound is a class (.cN, div p .cN):
                    the shape of the slow Japanese EPUB of koreader issue
                    #15995. Node-local gates reject everything; this is
                    where memoized candidates shine.
  element-leading   element-rightmost descendant rules (div.iN p b): cannot
                    be rejected by node-local information, so these keep
                    requiring a full check() per element.
  mixed             class-leading plus element-leading descendant rules.
  element-rightmost class rules with an element rightmost (p .cN b): the
                    class is not leading, so no gate can use it.
  unique-classes    every element carries its own class: worst case for any
                    per-(element, class) memoization, still fine for gates.
"""

import argparse
import os

ELEMENTS_FMT = ('<p class="{cls}">Paragraph {i} with <b>bold</b> and '
                '<i>italic</i> text.</p>')
CLASSES = ['alpha', 'beta', 'gamma', 'delta', 'epsilon']


def elements(count):
    out = []
    for i in range(count):
        out.append(ELEMENTS_FMT.format(cls=CLASSES[i % len(CLASSES)], i=i))
    return out


def document(rules, elems):
    return ('<html><head><style>' + '\n'.join(rules) + '</style></head><body>'
            + '\n'.join(elems) + '</body></html>')


def gen_class_leading(n_elements=12000):
    # 8000 class-leading rules.
    rules = [f'.c{i} {{ text-indent: {i % 7}px; }}' for i in range(3000)]
    rules += [f'div p .c{i} {{ letter-spacing: {i % 3}px; }}' for i in range(3000)]
    rules += [f'.c{i}.w {{ word-spacing: {i % 5}px; }}' for i in range(2000)]
    return document(rules, elements(n_elements))


def gen_element_leading(n_elements=12000):
    # 8000 element-leading descendant rules.
    rules = [f'.c{i} {{ text-indent: {i % 7}px; }}' for i in range(4000)]
    rules += [f'div.i{i} p b {{ letter-spacing: {i % 3}px; }}' for i in range(4000)]
    return document(rules, elements(n_elements))


def gen_mixed(n_elements=12000):
    # 6000 class-leading + 4000 element-leading descendant rules.
    rules = [f'.c{i} {{ text-indent: {i % 7}px; }}' for i in range(4000)]
    rules += [f'.c{i} p {{ letter-spacing: {i % 3}px; }}' for i in range(2000)]
    rules += [f'div .c{i} b {{ word-spacing: {i % 5}px; }}' for i in range(2000)]
    rules += [f'div.i{i} p b {{ vertical-align: {i % 4}px; }}' for i in range(2000)]
    return document(rules, elements(n_elements))


def gen_element_rightmost(n_elements=12000):
    # Class rules whose rightmost compound is an element: no leading class.
    rules = [f'.c{i} {{ text-indent: {i % 7}px; }}' for i in range(4000)]
    rules += [f'div p .c{i} {{ letter-spacing: {i % 3}px; }}' for i in range(2000)]
    rules += [f'p .c{i} b {{ word-spacing: {i % 5}px; }}' for i in range(2000)]
    return document(rules, elements(n_elements))


def gen_unique_classes(n_elements=6000):
    # Every element carries its own class out of 15000 defined ones.
    rules = [f'.c{i} {{ text-indent: {i % 7}px; }}' for i in range(15000)]
    rules += [f'div.d{i} > p i{i} > b {{ letter-spacing: {i % 3}px; }}'
              for i in range(7500)]
    rules += [f'p.a{i} .c{i % 3000} {{ word-spacing: {i % 5}px; }}'
              for i in range(7500)]
    elems = []
    for i in range(n_elements):
        if i % 3 == 0:
            elems.append(f'<p class="c{i % 15000}">Paragraph {i} with '
                         '<b>bold</b> and <i>italic</i>.</p>')
        elif i % 3 == 1:
            elems.append(f'<div class="d{i % 7500}"><p class="a{i % 7500}">'
                         f'Wrapped paragraph {i} with <b>bold</b> and '
                         '<i>italic</i>.</p></div>')
        else:
            elems.append(f'<p>Plain paragraph {i} with <b>bold</b> and '
                         '<i>italic</i>.</p>')
    return document(rules, elems)


GENERATORS = {
    'class-leading': gen_class_leading,
    'element-leading': gen_element_leading,
    'mixed': gen_mixed,
    'element-rightmost': gen_element_rightmost,
    'unique-classes': gen_unique_classes,
}


def main():
    parser = argparse.ArgumentParser(
        description='Generate synthetic CSS stress documents for benchmarking style application.',
        epilog='profiles: ' + ', '.join(GENERATORS))
    parser.add_argument('profile', choices=GENERATORS)
    parser.add_argument('-o', '--output', help='output file (default: stdout)')
    parser.add_argument('-n', '--elements', type=int,
                        help='number of elements (profile default otherwise)')
    args = parser.parse_args()
    gen = GENERATORS[args.profile]
    html = gen(args.elements) if args.elements else gen()
    if args.output:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, 'w') as f:
            f.write(html)
        print(f'{args.profile}: {os.path.getsize(args.output)} bytes -> {args.output}')
    else:
        print(html)


if __name__ == '__main__':
    main()
