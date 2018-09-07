#!/usr/bin/env python
#-*- coding: utf-8 -*-
"""
File    : a.py
Project :
Author  : ze00
Email   : zerozakiGeek@gmail.com
"""
print(list(reversed([int(i) for i in open("lightsMap").read().split()])))
