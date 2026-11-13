# OSIS schema

What `osis_schema_test` validates Milah's OSIS output against.

- `osisCore.2.1.1.xsd` — the OSIS 2.1.1 schema (Bible Technologies Group,
  5 March 2006), as published at <https://www.crosswire.org/osis/>. It states
  that it is "freely available for all purposes". The one change: its import of
  the `xml:` namespace points at the local `xml.xsd` instead of
  `http://www.w3.org/2001/xml.xsd`, so validation needs no network.
- `xml.xsd` — the W3C schema for the `xml:` namespace (`xml:lang`,
  `xml:space`), from <https://www.w3.org/2001/xml.xsd>, under the W3C software
  and document licence.
