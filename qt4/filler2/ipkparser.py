# -*- coding: utf-8 -*-

import xml.sax
import copy

class IPKParser:
	def __init__(self, filename):
		with open(filename) as fd:
			xml_parser = xml.sax.make_parser()
			handler = IPKParser.IPKDrawSetsHandler()
			xml_parser.setContentHandler(handler)
			xml_parser.setFeature(xml.sax.handler.feature_namespaces, True)
			xml_parser.parse(fd)
			self.ipk_conf = handler.sets
			self.ipk_title = handler.title

	def getTitle(self):
		return self.ipk_title

	def getSets(self):
		return self.ipk_conf.keys()

	def getParams(self, iset):
		return self.ipk_conf[iset]

	class IPKDrawSetsHandler(xml.sax.ContentHandler):
		def __init__(self):
			xml.sax.ContentHandler.__init__(self)
			self.sets = dict()

		def startElementNS(self, name, qname, attrs):
			(uri, name) = name

			if uri == 'http://www.praterm.com.pl/SZARP/ipk':
				if name == 'params':
					self.title = copy.copy(attrs.getValueByQName('title'))
				if name == 'param':
					self.param_name = copy.copy(attrs.getValueByQName('name'))
					try:
						self.param_draw_name = copy.copy(attrs.getValueByQName('draw_name'))
					except KeyError:
						pass
				elif name == 'draw':
					if attrs.getValueByQName('title') not in self.sets:
						self.sets[attrs.getValueByQName('title')] = []

					self.sets[attrs.getValueByQName('title')].append((self.param_name, self.param_draw_name))

