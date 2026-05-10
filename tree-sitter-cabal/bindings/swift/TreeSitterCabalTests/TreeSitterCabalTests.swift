import XCTest
import SwiftTreeSitter
import TreeSitterCabal

final class TreeSitterCabalTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_cabal())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Cabal grammar")
    }
}
